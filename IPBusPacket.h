// -*- c++ -*-

#ifndef IPBUSPACKET_H
#define IPBUSPACKET_H

#include <QObject>
#include "IPbusHeaders.h"
// needs more include files

const quint16 maxPacket = 368; //368 words, limit from ethernet MTU of 1500 bytes
enum errorType {networkError = 0, IPbusError = 1, logicError = 2};
static const char *errorTypeName[3] = {"Network error" , "IPbus error", "Logic error"};

class IPBusPacket : public QObject {
  Q_OBJECT

public:
  IPBusPacket() {
      _request[0] = PacketHeader(control, 0);
  }
  ~IPBusPacket() {}

  quint16 requestSize() const {return _requestSize; }
  quint16 responseSize() const {return _responseSize; }
  quint16& responseSize() {return _responseSize; }
  quint32* request() { return _request; }
  quint32* response() { return _response; }
  const StatusPacket& statusRequest() const {return  _statusRequest; }
  const StatusPacket& statusResponse() const { return _statusResponse; }
  QList<Transaction>& transactionsList() { return _transactionsList; }

  template<class Q>
  void addTransaction(Q *q, TransactionType type, quint32 address, quint32 *data, quint8 nWords = 1) {
    Transaction currentTransaction;
    _request[_requestSize] = TransactionHeader(type, nWords, _transactionsList.size());
    currentTransaction.requestHeader = (TransactionHeader *)(_request + _requestSize++);
    _request[_requestSize] = address;
    currentTransaction.address = _request + _requestSize++;
    currentTransaction.responseHeader = (TransactionHeader *)(_response + _responseSize++);
    switch (type) {
    case                read:
    case nonIncrementingRead:
    case   configurationRead:
      currentTransaction.data = data;
      _responseSize += nWords;
      break;
    case                write:
    case nonIncrementingWrite:
    case   configurationWrite:
      currentTransaction.data = _request + _requestSize;
      for (quint8 i=0; i<nWords; ++i) _request[_requestSize++] = data[i];
      break;
    case RMWbits:
      _request[_requestSize++] = data[0]; //AND term
      _request[_requestSize++] = data[1]; // OR term
      currentTransaction.data = _response + _responseSize++;
      break;
    case RMWsum:
      _request[_requestSize++] = *data; //addend
      currentTransaction.data = _response + _responseSize++;
      break;
    default:
      emit q->error("unknown transaction type", IPbusError);
    }
    if (_requestSize > maxPacket || _responseSize > maxPacket) {
      emit q->error("packet size exceeded", IPbusError);
      return;
    } else _transactionsList.append(currentTransaction);
  }
  template<class Q>
  void addWordToWrite(Q *q, quint32 address, quint32 value) {
    addTransaction(q, write, address, &value, 1);
  }
  template<class Q>
  bool processResponse(Q *q) { //check transactions successfulness and copy read data to destination
    for (quint16 i=0; i<_transactionsList.size(); ++i) {
      TransactionHeader *th = _transactionsList.at(i).responseHeader;
      if (th->ProtocolVersion != 2 || th->TransactionID != i || th->TypeID != _transactionsList.at(i).requestHeader->TypeID) {
        emit q->error(QString::asprintf("unexpected transaction header: %08X, expected: %08X", *th, *_transactionsList.at(i).requestHeader & 0xFFFFFFF0), IPbusError);
        return false;
      }
      if (th->Words > 0) switch (th->TypeID) {
        case                read:
        case nonIncrementingRead:
        case   configurationRead:
          if (_transactionsList.at(i).data != nullptr) {
            quint32 *src = (quint32 *)th + 1, *dst = _transactionsList.at(i).data;
            while (src <= (quint32 *)th + th->Words && src < _response + _responseSize) *dst++ = *src++;
          }
          if ((quint32 *)th + th->Words >= _response + _responseSize) { //response too short to contain nWords values
            emit q->successfulRead(_response + _responseSize - (quint32 *)th - 1);
            emit q->error("read transaction truncated", IPbusError);
            return false;
          } else
            emit q->successfulRead(th->Words);
          break;
        case RMWbits:
        case RMWsum :
          if (th->Words != 1) {
            emit q->error("wrong RMW transaction", IPbusError);
            return false;
          }
          emit q->successfulRead(1);
          /* fall through */ //[[fallthrough]];
        case                write:
        case nonIncrementingWrite:
        case   configurationWrite:
          emit q->successfulWrite(th->Words);
          break;
        default:
          emit q->error("unknown transaction type", IPbusError);
          return false;
        }
      if (th->InfoCode != 0) {
        debugPrint();
        emit q->error(th->infoCodeString() + QString::asprintf(", address: %08X", *_transactionsList.at(i).address + th->Words), IPbusError);
        return false;
      }
    }
    return true;
  }

  void debugPrint() const {
    qDebug("request:");
    for (quint16 i=0; i<_requestSize; ++i) {
      qDebug("%08X", _request[i]);
    }
    qDebug("        response:");
    for (quint16 i=0; i<_responseSize; ++i) {
      qDebug("        %08X", _response[i]);
    }
  }
  void resetTransactions() { //return to initial (default) state
    _requestSize = 1;
    _responseSize = 1;
    _transactionsList.clear();
  }
protected:
private:
  const StatusPacket _statusRequest;
  StatusPacket _statusResponse;
  QList<Transaction> _transactionsList;
  quint16 _requestSize = 1, _responseSize = 1; //values are measured in words
  quint32 _request[maxPacket], _response[maxPacket];
} ;

#endif // IPBUSPACKET_H
