
#include <QtSerialPort/QSerialPort>

#include <modbus/tools/modbus_serial_client.h>

namespace modbus {
class QtSerialPort : public AbstractSerialPort {
  Q_OBJECT
public:
  explicit QtSerialPort(QObject *parent = nullptr) : AbstractSerialPort(this) {
    setupEnvironment();
  }
  ~QtSerialPort() {}

  bool
  setBaudRate(qint32 baudRate,
              QSerialPort::Directions directions = QSerialPort::AllDirections) {
    return serialPort_.setBaudRate(baudRate, directions);
  }

  bool setDataBits(QSerialPort::DataBits dataBits) {
    return serialPort_.setDataBits(dataBits);
  }

  bool setParity(QSerialPort::Parity parity) {
    return serialPort_.setParity(parity);
  }

  bool setStopBits(QSerialPort::StopBits stopBits) {
    return serialPort_.setStopBits(stopBits);
  }

  void setPortName(const QString &name) { serialPort_.setPortName(name); }

  void open() override {
    bool success = serialPort_.open(QIODevice::ReadWrite);
    if (!success) {
      return;
    }
    emit opened();
  }

  void close() override {
    if (serialPort_.isOpen()) {
      serialPort_.close();
      return;
    }
    emit closed();
  }

  void write(const char *data, size_t size) override {
    serialPort_.write(data, size);
  }

  QByteArray readAll() override { return serialPort_.readAll(); }

  void clear() override { serialPort_.clear(); }

private:
  void setupEnvironment() {
    connect(&serialPort_, &QSerialPort::aboutToClose, this,
            [&]() { emit closed(); });
    connect(&serialPort_, &QSerialPort::errorOccurred, this,
            [&](QSerialPort::SerialPortError err) {
              if (err == QSerialPort::SerialPortError::NoError) {
                return;
              }
              emit error(serialPort_.portName() + ": " +
                         serialPort_.errorString());
            });
    connect(&serialPort_, &QSerialPort::bytesWritten, this,
            &QtSerialPort::bytesWritten);
    connect(&serialPort_, &QSerialPort::readyRead, this,
            &QtSerialPort::readyRead);
  }

  QSerialPort serialPort_;
};

QSerialClient *
newQtSerialClient(const QString &serialName, QSerialPort::BaudRate baudRate,
                QSerialPort::DataBits dataBits, QSerialPort::Parity parity,
                QSerialPort::StopBits stopBits, QObject *parent) {
  QtSerialPort *port = new QtSerialPort(parent);
  port->setBaudRate(baudRate);
  port->setDataBits(dataBits);
  port->setParity(parity);
  port->setStopBits(stopBits);
  port->setPortName(serialName);

  QSerialClient *client = new QSerialClient(port, parent);
  return client;
}
#include "modbus_qt_serialport.moc"

} // namespace modbus
