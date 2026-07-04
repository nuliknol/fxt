#include "api_client.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

api_client::api_client(QObject *parent)
	: QObject(parent),
	  m_base_url("http://127.0.0.1"),
	  m_timeout_ms(15000),
	  m_net(new QNetworkAccessManager(this))
{
}

void api_client::set_base_url(const QString &url)
{
	m_base_url = url.trimmed();
}

QString api_client::base_url() const
{
	return m_base_url;
}

void api_client::set_timeout_ms(int timeout_ms)
{
	m_timeout_ms = timeout_ms > 0 ? timeout_ms : 15000;
}

int api_client::timeout_ms() const
{
	return m_timeout_ms;
}

bool api_client::get(const QString &path_with_query, QJsonDocument *out_doc, QString *out_error)
{
	return request_json("GET", path_with_query, QByteArray(), out_doc, out_error);
}

bool api_client::post(const QString &path_with_query, QJsonDocument *out_doc, QString *out_error)
{
	return request_json("POST", path_with_query, QByteArray(), out_doc, out_error);
}

bool api_client::post_json(const QString &path_with_query, const QJsonDocument &body_doc, QJsonDocument *out_doc, QString *out_error)
{
	return request_json("POST", path_with_query, body_doc.toJson(QJsonDocument::Compact), out_doc, out_error);
}

bool api_client::request_json(const QString &method,
	const QString &path_with_query,
	const QByteArray &body,
	QJsonDocument *out_doc,
	QString *out_error)
{
	QUrl url(m_base_url + path_with_query);
	QNetworkRequest req(url);
	QNetworkReply *reply = nullptr;
	QEventLoop loop;
	QTimer timer;

	req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	if (method == "GET") {
		reply = m_net->get(req);
	} else {
		reply = m_net->post(req, body);
	}

	timer.setSingleShot(true);
	timer.start(m_timeout_ms);
	connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();

	if (timer.isActive()) {
		timer.stop();
	} else {
		reply->abort();
		if (out_error != nullptr) {
			*out_error = "request timeout";
		}
		reply->deleteLater();
		return false;
	}

	if (reply->error() != QNetworkReply::NoError) {
		if (out_error != nullptr) {
			*out_error = reply->errorString();
		}
		reply->deleteLater();
		return false;
	}

	QByteArray response_body = reply->readAll();
	QJsonParseError parse_error;
	QJsonDocument doc = QJsonDocument::fromJson(response_body, &parse_error);
	reply->deleteLater();

	if (parse_error.error != QJsonParseError::NoError) {
		if (out_error != nullptr) {
			*out_error = QString("json parse error: %1").arg(parse_error.errorString());
		}
		return false;
	}
	if (out_doc != nullptr) {
		*out_doc = doc;
	}
	return true;
}
