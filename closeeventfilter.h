#ifndef CLOSEEVENTFILTER_H
#define CLOSEEVENTFILTER_H

#include <QObject>

class CloseEventFilter : public QObject
{
    Q_OBJECT
public:
    explicit CloseEventFilter(QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

signals:
    void close(QObject *obj, QEvent *event);
};

#endif // CLOSEEVENTFILTER_H
