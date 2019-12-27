#include "modbus_client_p.h"
#include <QTimer>
#include <algorithm>
#include <assert.h>
#include <base/modbus_logger.h>
#include <modbus/base/modbus_tool.h>
#include <modbus/base/single_bit_access.h>
#include <modbus/base/smart_assert.h>
#include <modbus_frame.h>

namespace modbus {

static void appendQByteArray(ByteArray &array, const QByteArray &qarray);
static QVector<SixteenBitValue>
toSixteenBitValueList(const SixteenBitAccess &access);
static QVector<BitValue> toBitValueList(const SingleBitAccess &access);
std::shared_ptr<Frame> createModebusFrame(TransferMode mode);

QModbusClient::QModbusClient(AbstractIoDevice *iodevice, QObject *parent)
    : d_ptr(new QModbusClientPrivate(iodevice, this)), QObject(parent) {
  initMemberValues();
  setupEnvironment();
}

QModbusClient::QModbusClient(QObject *parent)
    : d_ptr(new QModbusClientPrivate(nullptr, parent)), QObject(parent) {
  initMemberValues();
  setupEnvironment();
}

QModbusClient::~QModbusClient() {}

void QModbusClient::open() {
  Q_D(QModbusClient);

  d->device_.open();
  return;
}

/**
 * Allows shutdown and transmits clientClosed signal regardless of whether the
 * device is already turned on
 */
void QModbusClient::close() {
  Q_D(QModbusClient);
  d->device_.close();
}

void QModbusClient::sendRequest(const Request &request) {
  Q_D(QModbusClient);

  if (!isOpened()) {
    log(LogLevel::kWarning, d->device_.name() + " closed, discard reuqest");
    return;
  }

  /*just queue the request, when the session state is in idle, it will be sent
   * out*/
  auto element = createElement(request);

  element.requestFrame = createModebusFrame(d->transferMode_);
  element.requestFrame->setAdu(element.request);

  element.responseFrame = createModebusFrame(d->transferMode_);
  element.responseFrame->setAdu(element.response);

  element.retryTimes = d->retryTimes_;
  d->enqueueElement(element);
}

void QModbusClient::readSingleBits(ServerAddress serverAddress,
                                   FunctionCode functionCode,
                                   Address startAddress, Quantity quantity) {
  if (functionCode != FunctionCode::kReadCoils &&
      functionCode != FunctionCode::kReadInputDiscrete) {
    log(LogLevel::kWarning, "single bit access:[read] invalid function code(" +
                                std::to_string(functionCode) + ")");
  }

  static const DataChecker dataChecker = {bytesRequired<4>,
                                          bytesRequiredStoreInArrayIndex0};

  SingleBitAccess access;

  access.setStartAddress(startAddress);
  access.setQuantity(quantity);

  auto request = createRequest(serverAddress, functionCode, dataChecker, access,
                               access.marshalReadRequest());
  sendRequest(request);
}

void QModbusClient::writeSingleCoil(ServerAddress serverAddress,
                                    Address startAddress, BitValue value) {
  static const DataChecker dataChecker = {bytesRequired<4>, bytesRequired<4>};

  SingleBitAccess access;

  access.setStartAddress(startAddress);
  access.setQuantity(1);
  access.setValue(value);
  auto request =
      createRequest(serverAddress, FunctionCode::kWriteSingleCoil, dataChecker,
                    access, access.marshalSingleWriteRequest());
  sendRequest(request);
}
}

void QModbusClient::readRegisters(ServerAddress serverAddress,
                                  FunctionCode functionCode,
                                  Address startAddress, Quantity quantity) {
  SixteenBitAccess access;

  access.setStartAddress(startAddress);
  access.setQuantity(quantity);
  sendRequest(createReadRegistersRequest(serverAddress, functionCode, access));
}

void QModbusClient::writeSingleRegister(ServerAddress serverAddress,
                                        Address address,
                                        const SixteenBitValue &value) {
  SixteenBitAccess access;

  access.setStartAddress(address);
  access.setValue(value.toUint16());
  sendRequest(createWriteSingleRegisterRequest(serverAddress, access));
}

void QModbusClient::writeMultipleRegisters(
    ServerAddress serverAddress, Address startAddress,
    const QVector<SixteenBitValue> &valueList) {
  SixteenBitAccess access;

  access.setStartAddress(startAddress);
  access.setQuantity(valueList.size());

  int offset = 0;
  for (const auto &sixValue : valueList) {
    auto address = access.startAddress() + offset;
    access.setValue(address, sixValue.toUint16());
    offset++;
  }
  sendRequest(createWriteMultipleRegisterRequest(serverAddress, access));
}

bool QModbusClient::isIdle() {
  Q_D(QModbusClient);
  return d->sessionState_.state() == SessionState::kIdle;
}

bool QModbusClient::isClosed() {
  Q_D(QModbusClient);
  return d->device_.isClosed();
}

bool QModbusClient::isOpened() {
  Q_D(QModbusClient);
  return d->device_.isOpened();
}

void QModbusClient::setupEnvironment() {
  qRegisterMetaType<Request>("Request");
  qRegisterMetaType<Response>("Response");
  qRegisterMetaType<SixteenBitAccess>("SixteenBitAccess");
  qRegisterMetaType<ServerAddress>("ServerAddress");
  qRegisterMetaType<Address>("Address");
  qRegisterMetaType<Error>("Error");
  qRegisterMetaType<QVector<SixteenBitValue>>("QVector<SixteenBitValue>");
  qRegisterMetaType<QVector<BitValue>>("QVector<BitValue>");

  Q_D(QModbusClient);

  connect(&d->device_, &ReconnectableIoDevice::opened, this,
          &QModbusClient::clientOpened);
  connect(&d->device_, &ReconnectableIoDevice::closed, this,
          &QModbusClient::clientClosed);
  connect(&d->device_, &ReconnectableIoDevice::error, this,
          &QModbusClient::clearPendingRequest);
  connect(&d->device_, &ReconnectableIoDevice::connectionIsLostWillReconnect,
          this, &QModbusClient::clearPendingRequest);
  connect(&d->device_, &ReconnectableIoDevice::error, this,
          &QModbusClient::onIoDeviceError);
  connect(&d->device_, &ReconnectableIoDevice::bytesWritten, this,
          &QModbusClient::onIoDeviceBytesWritten);
  connect(&d->device_, &ReconnectableIoDevice::readyRead, this,
          &QModbusClient::onIoDeviceReadyRead);
  connect(&d->waitResponseTimer_, &QTimer::timeout, this,
          &QModbusClient::onIoDeviceResponseTimeout);
  connect(this, &QModbusClient::requestFinished, this,
          &QModbusClient::processResponseAnyFunctionCode);
}

void QModbusClient::setTimeout(uint64_t timeout) {
  Q_D(QModbusClient);

  d->waitResponseTimeout_ = timeout;
}

uint64_t QModbusClient::timeout() {
  Q_D(QModbusClient);

  return d->waitResponseTimeout_;
}

void QModbusClient::setTransferMode(TransferMode transferMode) {
  Q_D(QModbusClient);

  d->transferMode_ = transferMode;
}

TransferMode QModbusClient::transferMode() const {
  const Q_D(QModbusClient);

  return d->transferMode_;
}

void QModbusClient::setRetryTimes(int times) {
  Q_D(QModbusClient);

  d->retryTimes_ = std::max(0, times);
}

int QModbusClient::retryTimes() {
  Q_D(QModbusClient);
  return d->retryTimes_;
}

void QModbusClient::setOpenRetryTimes(int retryTimes, int delay) {
  Q_D(QModbusClient);
  d->device_.setOpenRetryTimes(retryTimes, delay);
}

int QModbusClient::openRetryTimes() {
  Q_D(QModbusClient);
  return d->device_.openRetryTimes();
}

int QModbusClient::openRetryDelay() {
  Q_D(QModbusClient);
  return d->device_.openRetryDelay();
}

void QModbusClient::setFrameInterval(int frameInterval) {
  Q_D(QModbusClient);
  if (frameInterval < 0) {
    frameInterval = 0;
  }
  d->t3_5_ = frameInterval;
}

void QModbusClient::clearPendingRequest() {
  Q_D(QModbusClient);
  while (!d->elementQueue_.empty()) {
    d->elementQueue_.pop();
  }
}

size_t QModbusClient::pendingRequestSize() {
  Q_D(QModbusClient);
  return d->elementQueue_.size();
}

QString QModbusClient::errorString() {
  Q_D(QModbusClient);
  return d->errorString_;
}

void QModbusClient::initMemberValues() {
  Q_D(QModbusClient);

  d->sessionState_.setState(SessionState::kIdle);
  d->waitConversionDelay_ = 200;
  d->t3_5_ = 60;
  d->waitResponseTimeout_ = 1000;
  d->retryTimes_ = 0; /// default no retry
  d->transferMode_ = TransferMode::kRtu;
}

void QModbusClient::onIoDeviceResponseTimeout() {
  Q_D(QModbusClient);
  assert(d->sessionState_.state() == SessionState::kWaitingResponse);

  auto &element = d->elementQueue_.front();
  element.bytesWritten = 0;

  /**
   *  An error occurs when the response times out but no response is
   * received. Then the master node enters the "idle" state and issues a
   * retry request. The maximum number of retries depends on the settings
   * of the primary node
   *
   */
  d->sessionState_.setState(SessionState::kIdle);

  if (element.retryTimes-- > 0) {
    log(LogLevel::kWarning,
        d->device_.name() + " waiting response timeout, retry it, retrytimes " +
            std::to_string(element.retryTimes));
  } else {
    log(LogLevel::kWarning, d->device_.name() + ": waiting response timeout");

    auto request = element.request;
    auto response = element.response;
    /**
     * if have no retry times, remove this request
     */
    d->elementQueue_.pop();
    response.setError(Error::kTimeout);
    emit requestFinished(request, response);
  }
  d->scheduleNextRequest(d->t3_5_);
}

void QModbusClient::onIoDeviceReadyRead() {
  Q_D(QModbusClient);

  /**
   * When the last byte of the request is sent, it will enter the wait-response
   * state. Therefore, if data is received but not in the wait-response state,
   * then this data is not what we want,discard them
   */
  auto qdata = d->device_.readAll();
  if (d->sessionState_.state() != SessionState::kWaitingResponse) {
    ByteArray data;
    appendQByteArray(data, qdata);
    std::stringstream stream;
    stream << d->sessionState_.state();
    log(LogLevel::kWarning,
        d->device_.name() + " now state is in " + stream.str() +
            ".got unexpected data, discard them." + "[" + d->dump(data) + "]");

    d->device_.clear();
    return;
  }

  auto &element = d->elementQueue_.front();
  auto &dataRecived = element.dataRecived;
  auto request = element.request;
  auto response = element.response;

  auto sessionState = d->sessionState_.state();

  appendQByteArray(dataRecived, qdata);

  Error error = Error::kNoError;
  auto result = element.responseFrame->unmarshal(dataRecived, &error);
  if (result != DataChecker::Result::kSizeOk) {
    log(LogLevel::kWarning, d->device_.name() + ":need more data." + "[" +
                                d->dump(dataRecived) + "]");
    return;
  }

  response = Response(element.responseFrame->adu());
  response.setError(error);

  /**
   * When receiving a response from an undesired child node,
   * Should continue to time out
   * discard all recived dat
   */
  if (response.serverAddress() != request.serverAddress()) {
    log(LogLevel::kWarning,
        d->device_.name() +
            ":got response, unexpected serveraddress, discard it.[" +
            d->dump(dataRecived) + "]");

    dataRecived.clear();
    return;
  }

  d->waitResponseTimer_.stop();
  d->sessionState_.setState(SessionState::kIdle);

  log(LogLevel::kDebug, d->device_.name() + " recived " + d->dump(dataRecived));

  /**
   * Pop at the end
   */
  d->elementQueue_.pop();
  emit requestFinished(request, response);
  d->scheduleNextRequest(d->t3_5_);
}

void QModbusClient::onIoDeviceBytesWritten(qint16 bytes) {
  Q_D(QModbusClient);

  assert(d->sessionState_.state() == SessionState::kSendingRequest &&
         "when write operation is not done, the session state must be in "
         "kSendingRequest");

  /*check the request is sent done*/
  auto &element = d->elementQueue_.front();
  auto &request = element.request;
  element.bytesWritten += bytes;
  if (element.bytesWritten != element.requestFrame->marshalSize()) {
    return;
  }

  if (request.isBrocast()) {
    d->elementQueue_.pop();
    d->sessionState_.setState(SessionState::kIdle);
    d->scheduleNextRequest(d->waitConversionDelay_);

    log(LogLevel::kWarning,
        d->device_.name() + " brocast request, turn into idle status");
    return;
  }

  /**
   * According to the modebus rtu master station state diagram, when the
   * request is sent to the child node, the response timeout timer is
   * started. If the response times out, the next step is to retry. After
   * the number of retries is exceeded, error processing is performed
   * (return to the user).
   */
  d->sessionState_.setState(SessionState::kWaitingResponse);
  d->waitResponseTimer_.setSingleShot(true);
  d->waitResponseTimer_.setInterval(d->waitResponseTimeout_);
  d->waitResponseTimer_.start();
}

void QModbusClient::onIoDeviceError(const QString &errorString) {
  Q_D(QModbusClient);

  d->errorString_ = errorString;

  switch (d->sessionState_.state()) {
  case SessionState::kWaitingResponse:
    d->waitResponseTimer_.stop();
  default:
    break;
  }

  d->sessionState_.setState(SessionState::kIdle);
  emit errorOccur(errorString);
}

void QModbusClient::processResponseAnyFunctionCode(const Request &request,
                                                   const Response &response) {
  switch (request.functionCode()) {
  case FunctionCode::kReadCoils:
  case FunctionCode::kReadInputDiscrete: {
    auto access = modbus::any::any_cast<SingleBitAccess>(request.userData());
    if (!response.isException()) {
      processReadSingleBit(request, response, &access);
    }
    emit readSingleBitsFinished(request.serverAddress(), access.startAddress(),
                                toBitValueList(access), response.error());
    return;
  }
  case FunctionCode::kWriteSingleCoil: {
    auto access = modbus::any::any_cast<SingleBitAccess>(request.userData());
    emit writeSingleCoilFinished(request.serverAddress(), access.startAddress(),
                                 response.error());
    return;
  }
  case FunctionCode::kReadHoldingRegisters:
  case FunctionCode::kReadInputRegister: {
    auto access = modbus::any::any_cast<SixteenBitAccess>(request.userData());
    if (!response.isException()) {
      processReadRegisters(request, response, &access);
    }
    emit readRegistersFinished(request.serverAddress(), access.startAddress(),
                               toSixteenBitValueList(access), response.error());
    return;
  }
  case FunctionCode::kWriteSingleRegister: {
    auto access = modbus::any::any_cast<SixteenBitAccess>(request.userData());
    emit writeSingleRegisterFinished(request.serverAddress(),
                                     access.startAddress(), response.error());
    return;
  }
  case FunctionCode::kWriteMultipleRegisters: {
    auto access = modbus::any::any_cast<SixteenBitAccess>(request.userData());
    emit writeMultipleRegistersFinished(
        request.serverAddress(), access.startAddress(), response.error());
    return;
  }
  default:
    return;
  }
} // namespace modbus

static void appendQByteArray(ByteArray &array, const QByteArray &qarray) {
  uint8_t *data = (uint8_t *)qarray.constData();
  size_t size = qarray.size();
  array.insert(array.end(), data, data + size);
}

static QVector<SixteenBitValue>
toSixteenBitValueList(const SixteenBitAccess &access) {
  QVector<SixteenBitValue> valueList;

  for (size_t i = 0; i < access.quantity(); i++) {
    Address address = access.startAddress() + i;

    bool found = true;
    SixteenBitValue value = access.value(address, &found);
    if (!found) {
      continue;
    }
    valueList.push_back(value);
  }
  return valueList;
}

static QVector<BitValue> toBitValueList(const SingleBitAccess &access) {
  QVector<BitValue> valueList;

  for (size_t i = 0; i < access.quantity(); i++) {
    Address address = access.startAddress() + i;

    BitValue value = access.value(address);
    if (value == BitValue::kBadValue) {
      continue;
    }
    valueList.push_back(value);
  }
  return valueList;
}

std::shared_ptr<Frame> createModebusFrame(TransferMode mode) {
  switch (mode) {
  case TransferMode::kRtu:
    return std::make_shared<RtuFrame>();
  case TransferMode::kAscii:
    return std::make_shared<AsciiFrame>();
  case TransferMode::kMbap:
    return std::make_shared<MbapFrame>();
  default:
    smart_assert("unsupported modbus transfer mode")(static_cast<int>(mode));
    return nullptr;
  }
}

} // namespace modbus
