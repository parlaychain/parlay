#ifndef PRIMENODEMANAGER_H
#define PRIMENODEMANAGER_H

#include "util.h"
#include "sync.h"

#include <QMenu>
#include <QWidget>
#include <QTimer>
#include <QItemSelectionModel>

namespace Ui {
    class PrimenodeManager;

}
class ClientModel;
class WalletModel;
class QAbstractItemView;
class QItemSelectionModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Primenode Manager page widget */
class PrimenodeManager : public QWidget
{
    Q_OBJECT

public:
    explicit PrimenodeManager(QWidget *parent = 0);
    ~PrimenodeManager();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);

private:
    QMenu* contextMenu;
    
public slots:
    void updateNodeList();
    void updateAdrenalineNode(QString alias, QString addr, QString privkey, QString txHash, QString txIndex, QString rewardAddress, QString rewardPercentage, QString status);
    void on_UpdateButton_clicked();
    void copyAddress();
    void copyPubkey();

signals:

private:
    QTimer *timer;
    Ui::PrimenodeManager *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    CCriticalSection cs_adrenaline;

private slots:
    void showContextMenu(const QPoint&);
    void on_createButton_clicked();
    void on_startButton_clicked();
    void on_startAllButton_clicked();
    void on_tableWidget_2_itemSelectionChanged();
};
#endif // PRIMENODEMANAGER_H
