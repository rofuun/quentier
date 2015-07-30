#ifndef __QUTE_NOTE__GUI__ASK_CONSUMER_KEY_AND_SECRET_H
#define __QUTE_NOTE__GUI__ASK_CONSUMER_KEY_AND_SECRET_H

#include <qute_note/utility/Qt4Helper.h>
#include <QWidget>
#include <QString>

namespace Ui {
class AskConsumerKeyAndSecret;
}

class AskConsumerKeyAndSecret: public QWidget
{
    Q_OBJECT
public:
    explicit AskConsumerKeyAndSecret(QWidget * parent = nullptr);
    virtual ~AskConsumerKeyAndSecret();

signals:
    void consumerKeyAndSecretEntered(QString key, QString secret);
    void cancelled(QString);

public slots:
    void onOkButtonPressed();
    void onCancelButtonPressed();

protected:
    virtual void keyPressEvent(QKeyEvent * pEvent) Q_DECL_OVERRIDE;

private:
    Ui::AskConsumerKeyAndSecret * m_pUI;
};

#endif // __QUTE_NOTE__GUI__ASK_CONSUMER_KEY_AND_SECRET_H