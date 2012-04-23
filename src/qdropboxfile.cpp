#include "qdropboxfile.h"

QDropboxFile::QDropboxFile(QObject *parent) :
    QIODevice(parent),
    _conManager(this)
{
    _buffer = NULL;
    _evLoop = NULL;
    _waitMode = notWaiting;
    connectSignals();
}

QDropboxFile::QDropboxFile(QDropbox *api, QObject *parent) :
    QIODevice(parent),
    _conManager(this)
{
    _api    = api;
    _evLoop = NULL;
    _buffer = NULL;
    _waitMode = notWaiting;

    obtainToken();
    connectSignals();
}

QDropboxFile::QDropboxFile(QString filename, QDropbox *api, QObject *parent) :
    QIODevice(parent),
    _conManager(this)
{
   _api      = api;
   _buffer   = NULL;
   _filename = filename;
   _evLoop = NULL;
   _waitMode = notWaiting;

   obtainToken();
   connectSignals();
}

QDropboxFile::~QDropboxFile()
{
    if(_buffer != NULL)
        delete _buffer;
    if(_evLoop != NULL)
        delete _evLoop;
}

bool QDropboxFile::isSequential()
{
    return false;
}

bool QDropboxFile::open(QIODevice::OpenMode mode)
{
#ifdef QTDROPBOX_DEBUG
    qDebug() << "QDropboxFile::open(...)" << endl;
#endif
    if(!QIODevice::open(mode))
        return false;

  /*  if(isMode(QIODevice::NotOpen))
        return true; */

    if(_buffer == NULL)
        _buffer = new QByteArray();

#ifdef QTDROPBOX_DEBUG
    qDebug() << "QDropboxFile: opening file" << endl;
#endif

    if(isMode(QIODevice::Truncate))
    {
#ifdef QTDROPBOX_DEBUG
    qDebug() << "QDropboxFile: _buffer cleared." << endl;
#endif
        _buffer->clear();
    }
    else
    {
#ifdef QTDROPBOX_DEBUG
    qDebug() << "QDropboxFile: reading file content" << endl;
#endif
        if(!getFileContent(_filename))
            return false;
    }

    return true;
}

void QDropboxFile::close()
{
}

void QDropboxFile::setApi(QDropbox *dropbox)
{
}

QDropbox *QDropboxFile::api()
{
    return _api;
}

void QDropboxFile::setFilename(QString filename)
{
    _filename = filename;
    return;
}

QString QDropboxFile::filename()
{
    return _filename;
}

bool QDropboxFile::flush()
{
    return true;
}

bool QDropboxFile::event(QEvent *event)
{
#ifdef QTDROPBOX_DEBUG
    qDebug() << "processing event: " << event->type() << endl;
#endif
    return QIODevice::event(event);
}

qint64 QDropboxFile::readData(char *data, qint64 maxlen)
{
#ifdef QTDROPBOX_DEBUG
    qDebug() << "QDropboxFile::readData(...), maxlen = " << maxlen << endl;
    QString buff_str = QString(*_buffer);
    qDebug() << "old bytes = " << _buffer->toHex() << ", str: " << buff_str <<  endl;
    qDebug() << "old size = " << _buffer->size() << endl;
#endif

    if(_buffer->size() == 0)
        return 0;

    if(_buffer->size() < maxlen)
        maxlen = _buffer->size();

    qint64 newsize = _buffer->size()-maxlen;
    //data = _buffer->left(maxlen).data();
    QByteArray tmp = _buffer->left(maxlen);
    char *d = tmp.data();
    memcpy(data, d, maxlen);
    QByteArray newbytes = _buffer->right(newsize);
    _buffer->clear();
    _buffer->append(newbytes);
#ifdef QTDROPBOX_DEBUG
    qDebug() << "new size = " << _buffer->size() << endl;
    qDebug() << "new bytes = " << _buffer->toHex() << endl;
#endif
    return maxlen;
}

qint64 QDropboxFile::writeData(const char *data, qint64 len)
{
    qint64 new_len = _buffer->size()+len;
    char *current_data = _buffer->data();
#ifdef QTDROPBOX_DEBUG
    qDebug() << "new content: " << _buffer->toHex() << endl;
#endif
    char *new_data     = new char[new_len];
    memcpy(new_data, current_data, _buffer->size());
    char *pNext = new_data+_buffer->size();
    memcpy(pNext, data, len);
    _buffer->setRawData(new_data, new_len);
#ifdef QTDROPBOX_DEBUG
    qDebug() << "new content: " << _buffer->toHex() << endl;
#endif
    return 0;
}

void QDropboxFile::networkRequestFinished(QNetworkReply *rply)
{
#ifdef QTDROPBOX_DEBUG
    qDebug() << "QDropboxFile::networkRequestFinished(...)" << endl;
#endif

    switch(_waitMode)
    {
    case waitForRead:
        rplyFileContent(rply);
        stopEventLoop();
        break;
    case waitForWrite:
        stopEventLoop();
        break;
    }
}

void QDropboxFile::obtainToken()
{
    _token       = _api->token();
    _tokenSecret = _api->tokenSecret();
    return;
}

void QDropboxFile::connectSignals()
{
    connect(&_conManager, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(networkRequestFinished(QNetworkReply*)));
    return;
}

bool QDropboxFile::isMode(QIODevice::OpenMode mode)
{
    return ( (openMode()&mode) == mode );
}

bool QDropboxFile::getFileContent(QString filename)
{
#ifdef QTDROPBOX_DEBUG
    qDebug() << "QDropboxFile::getFileContent(...)" << endl;
#endif
    QUrl request;
    request.setUrl(QDROPBOXFILE_CONTENT_URL, QUrl::StrictMode);
    request.setPath(QString("%1/files/%2")
                    .arg(_api->apiVersion().left(1))
                    .arg(filename));
    request.addQueryItem("oauth_consumer_key", _api->appKey());
    request.addQueryItem("oauth_nonce", QDropbox::generateNonce(128));
    request.addQueryItem("oauth_signature_method", _api->signatureMethodString());
    request.addQueryItem("oauth_timestamp", QString::number((int) QDateTime::currentMSecsSinceEpoch()/1000));
    request.addQueryItem("oauth_token", _api->token());
    request.addQueryItem("oauth_version", _api->apiVersion());

    QString signature = _api->oAuthSign(request);
    request.addQueryItem("oauth_signature", signature);

#ifdef QTDROPBOX_DEBUG
    qDebug() << "QDropboxFile::getFileContent " << request.toString() << endl;
#endif

    QNetworkRequest rq(request);
    _conManager.get(rq);

    _waitMode = waitForRead;
    startEventLoop();

    if(lastErrorCode != 0)
    {
#ifdef QTDROPBOX_DEBUG
        qDebug() << "QDropboxFile::getFileContent ReadError: " << lastErrorCode << lastErrorMessage << endl;
#endif
        return false;
    }

    return true;
}

void QDropboxFile::rplyFileContent(QNetworkReply *rply)
{
    lastErrorCode = 0;

    QByteArray response = rply->readAll();
    QString resp_str;
    QDropboxJson json;

#ifdef QTDROPBOX_DEBUG
    resp_str = QString(response.toHex());
    qDebug() << "QDropboxFile::rplyFileContent response = " << resp_str << endl;

#endif

    switch(rply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
    {
    case QDROPBOX_ERROR_BAD_INPUT:
    case QDROPBOX_ERROR_EXPIRED_TOKEN:
    case QDROPBOX_ERROR_BAD_OAUTH_REQUEST:
    case QDROPBOX_ERROR_FILE_NOT_FOUND:
    case QDROPBOX_ERROR_WRONG_METHOD:
    case QDROPBOX_ERROR_REQUEST_CAP:
    case QDROPBOX_ERROR_USER_OVER_QUOTA:
        resp_str = QString(response);
        json.parseString(response.trimmed());
        lastErrorCode = rply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
#ifdef QTDROPBOX_DEBUG
    qDebug() << "QDropboxFile::rplyFileContent jason.valid = " << json.isValid() << endl;
#endif
        if(json.isValid())
            lastErrorMessage = json.getString("error");
        else
            lastErrorMessage = "";
        return;
        break;
    default:
        break;
    }

    _buffer->clear();
    _buffer->append(response);
    emit readyRead();
    return;
}

void QDropboxFile::startEventLoop()
{
#ifdef QTDROPBOX_DEBUG
    qDebug() << "QDropboxFile::startEventLoop()" << endl;
#endif
    if(_evLoop == NULL)
        _evLoop = new QEventLoop(this);
    _evLoop->exec();
    return;
}

void QDropboxFile::stopEventLoop()
{
#ifdef QTDROPBOX_DEBUG
    qDebug() << "QDropboxFile::stopEventLoop()" << endl;
#endif
    if(_evLoop == NULL)
        return;
    _evLoop->exit();
    return;
}