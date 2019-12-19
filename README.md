# modbus
a modbus library for c++11,using qt

## required
   QT version >= 5.6

   gcc version >= 4.9

   vs version >= vs 2015

## features
   [x] modbus master serial client

   [x] rtu mode

   [x] disconnection and reconnection

   [x] request failed and retry

   [x] single bit access

   [x] sixteen bit access

   [x] custom functions

   [x] requests are not lost, all requests will be sent at least once

## build from source

  * windows

  ```cmd
  git clone --recursive https://github.com/paopaol/modbus.git
  cd modbus
  cmake -Bbuild -H. -G"Visual Studio 14 2015" -DCMAKE_PREFIX_PATH=%QTDIR%
  cmake --build build --config rlease
  ```

  * linux

  ```cmd
  git clone --recursive https://github.com/paopaol/modbus.git
  cd modbus
  cmake -Bbuild -H. -DCMAKE_PREFIX_PATH=$QTDIR
  cmake --build build --config rlease
  ```


## examples

* single bit access

```cpp
#include <QCoreApplication>
#include <QDebug>
#include <modbus/base/single_bit_access.h>
#include <modbus/tools/modbus_client.h>

static QString modbusBitValueToString(modbus::BitValue value);

static modbus::DataChecker newDataChecker() {
  modbus::DataChecker dataChecker;
  dataChecker.calculateRequestSize = modbus::bytesRequired<4>;
  dataChecker.calculateResponseSize = modbus::bytesRequiredStoreInArrayIndex0;
  return dataChecker;
}

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);

  QScopedPointer<modbus::QModbusClient> client(
      modbus::newQtSerialClient("/dev/ttyS0"));

  client->setOpenRetryTimes(5, 5000);

  QObject::connect(
      client.data(), &modbus::QModbusClient::errorOccur,
      [&](const QString &errorString) { qDebug() << errorString; });
  QObject::connect(client.data(), &modbus::QModbusClient::clientClosed,
                   [&]() { qDebug() << "client is closed"; });
  QObject::connect(client.data(), &modbus::QModbusClient::clientOpened, [&]() {
    qDebug() << "client is opened";

    modbus::Request request;

    modbus::SingleBitAccess access;
    access.setStartAddress(0);
    access.setQuantity(5);

    request.setServerAddress(0);
    request.setFunctionCode(modbus::FunctionCode::kReadCoils);
    request.setUserData(access);
    request.setData(access.marshalReadRequest());
    request.setDataChecker(newDataChecker());

    client->sendRequest(request);
  });

  QObject::connect(
      client.data(), &modbus::QModbusClient::requestFinished,
      [&](const modbus::Request &req, const modbus::Response &resp) {
        if (resp.error() != modbus::Error::kNoError) {
          qDebug() << resp.errorString().c_str();
          return;
        }

        if (resp.isException()) {
          qDebug() << resp.errorString().c_str();
          return;
        }
        modbus::SingleBitAccess access =
            modbus::any::any_cast<modbus::SingleBitAccess>(req.userData());
        bool ok = access.unmarshalReadResponse(resp.data());
        if (!ok) {
          qDebug() << "data is invalid";
          return;
        }
        modbus::Address address = access.startAddress();
        for (int offset = 0; offset < access.quantity(); offset++) {
          modbus::Address currentAddress = address + offset;
          qDebug() << "address: " << currentAddress << " value: "
                   << modbusBitValueToString(access.value(currentAddress));
        }
      });

  client->open();

  return app.exec();
}

static QString modbusBitValueToString(modbus::BitValue value) {
  switch (value) {
  case modbus::BitValue::kOn:
    return "true";
  case modbus::BitValue::kOff:
    return "false";
  case modbus::BitValue::kBadValue:
    return "badValue";
  }
  return "";
}
```
