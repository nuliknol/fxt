#ifndef API_CLIENT_H
#define API_CLIENT_H

#include <QJsonDocument>
#include <QObject>
#include <QString>

class QNetworkAccessManager;

class api_client : public QObject
{
	Q_OBJECT

public:
	explicit api_client(QObject *parent = nullptr);

	void set_base_url(const QString &url);
	QString base_url() const;
	void set_timeout_ms(int timeout_ms);
	int timeout_ms() const;

	bool get(const QString &path_with_query, QJsonDocument *out_doc, QString *out_error);
	bool post(const QString &path_with_query, QJsonDocument *out_doc, QString *out_error);
	bool post_json(const QString &path_with_query, const QJsonDocument &body_doc, QJsonDocument *out_doc, QString *out_error);

private:
	bool request_json(const QString &method,
		const QString &path_with_query,
		const QByteArray &body,
		QJsonDocument *out_doc,
		QString *out_error);

	QString m_base_url;
	int m_timeout_ms;
	QNetworkAccessManager *m_net;
};

#endif
