#include "primenodemanager.h"
#include "ui_primenodemanager.h"
#include "addeditadrenalinenode.h"
#include "adrenalinenodeconfigdialog.h"

#include "sync.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "activeprimenode.h"
#include "primenodeconfig.h"
#include "primenodeman.h"
#include "primenode.h"
#include "walletdb.h"
#include "wallet.h"
#include "init.h"
#include "rpcserver.h"
#include <boost/lexical_cast.hpp>
#include <fstream>
using namespace json_spirit;
using namespace std;

#include <QAbstractItemDelegate>
#include <QClipboard>
#include <QPainter>
#include <QTimer>
#include <QDebug>
#include <QScrollArea>
#include <QScroller>
#include <QDateTime>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QItemSelectionModel>

PrimenodeManager::PrimenodeManager(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PrimenodeManager),
    clientModel(0),
    walletModel(0)
{
    ui->setupUi(this);

    ui->editButton->setEnabled(false);
    ui->startButton->setEnabled(false);

    int columnAddressWidth = 200;
    int columnProtocolWidth = 60;
    int columnStatusWidth = 80;
    int columnActiveWidth = 130;
    int columnLastSeenWidth = 130;
    
    ui->tableWidgetPrimenodes->setColumnWidth(0, columnAddressWidth);
    ui->tableWidgetPrimenodes->setColumnWidth(1, columnProtocolWidth);
    ui->tableWidgetPrimenodes->setColumnWidth(2, columnStatusWidth);
    ui->tableWidgetPrimenodes->setColumnWidth(3, columnActiveWidth);
    ui->tableWidgetPrimenodes->setColumnWidth(4, columnLastSeenWidth);
    
    ui->tableWidgetPrimenodes->setContextMenuPolicy(Qt::CustomContextMenu);
    QAction *copyAddressAction = new QAction(tr("Copy Address"), this);
    QAction *copyPubkeyAction = new QAction(tr("Copy Pubkey"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyPubkeyAction);
    connect(ui->tableWidgetPrimenodes, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
    connect(copyPubkeyAction, SIGNAL(triggered()), this, SLOT(copyPubkey()));
        
    ui->tableWidgetPrimenodes->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableWidget_2->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateNodeList()));
    if(!GetBoolArg("-reindexaddr", false))
        timer->start(30000);

    updateNodeList();
}

PrimenodeManager::~PrimenodeManager()
{
    delete ui;
}

void PrimenodeManager::on_tableWidget_2_itemSelectionChanged()
{
    if(ui->tableWidget_2->selectedItems().count() > 0)
    {
        ui->editButton->setEnabled(true);
        ui->startButton->setEnabled(true);
    }
}

void PrimenodeManager::updateAdrenalineNode(QString alias, QString addr, QString privkey, QString txHash, QString txIndex, QString rewardAddress, QString rewardPercentage, QString status)
{
    LOCK(cs_adrenaline);
    bool bFound = false;
    int nodeRow = 0;
    for(int i=0; i < ui->tableWidget_2->rowCount(); i++)
    {
        if(ui->tableWidget_2->item(i, 0)->text() == alias)
        {
            bFound = true;
            nodeRow = i;
            break;
        }
    }

    if(nodeRow == 0 && !bFound)
        ui->tableWidget_2->insertRow(0);

    QTableWidgetItem *aliasItem = new QTableWidgetItem(alias);
    QTableWidgetItem *addrItem = new QTableWidgetItem(addr);
    QTableWidgetItem *rewardAddressItem = new QTableWidgetItem(rewardAddress);
    QTableWidgetItem *rewardPercentageItem = new QTableWidgetItem(rewardPercentage);
    QTableWidgetItem *statusItem = new QTableWidgetItem(status);

    ui->tableWidget_2->setItem(nodeRow, 0, aliasItem);
    ui->tableWidget_2->setItem(nodeRow, 1, addrItem);
    ui->tableWidget_2->setItem(nodeRow, 2, rewardPercentageItem);
    ui->tableWidget_2->setItem(nodeRow, 3, rewardAddressItem);
    ui->tableWidget_2->setItem(nodeRow, 4, statusItem);
}

static QString seconds_to_DHMS(quint32 duration)
{
  QString res;
  int seconds = (int) (duration % 60);
  duration /= 60;
  int minutes = (int) (duration % 60);
  duration /= 60;
  int hours = (int) (duration % 24);
  int days = (int) (duration / 24);
  if((hours == 0)&&(days == 0))
      return res.sprintf("%02dm:%02ds", minutes, seconds);
  if (days == 0)
      return res.sprintf("%02dh:%02dm:%02ds", hours, minutes, seconds);
  return res.sprintf("%dd %02dh:%02dm:%02ds", days, hours, minutes, seconds);
}

void PrimenodeManager::updateNodeList()
{
    static int64_t nTimeListUpdated = GetTime();
    int64_t nSecondsToWait = nTimeListUpdated - GetTime() + 30;
    if (nSecondsToWait > 0) return;
    
    TRY_LOCK(cs_primenodes, lockPrimenodes);
    if(!lockPrimenodes) return;

    ui->countLabel->setText("Updating...");
    ui->tableWidgetPrimenodes->setSortingEnabled(false);
    ui->tableWidgetPrimenodes->clearContents();
    ui->tableWidgetPrimenodes->setRowCount(0);
    std::vector<CPrimenode> vPrimenodes = mnodeman.GetFullPrimenodeVector();
    
    BOOST_FOREACH(CPrimenode& mn, vPrimenodes)
    {

        // populate list
        // Address, Protocol, Status, Active Seconds, Last Seen, Pub Key
        QTableWidgetItem* addressItem = new QTableWidgetItem(QString::fromStdString(mn.addr.ToString()));
        QTableWidgetItem* protocolItem = new QTableWidgetItem(QString::number(mn.protocolVersion));
        QTableWidgetItem* statusItem = new QTableWidgetItem(QString::number(mn.IsEnabled()));
        QTableWidgetItem* activeSecondsItem = new QTableWidgetItem(seconds_to_DHMS((qint64)(mn.lastTimeSeen - mn.sigTime)));
        QTableWidgetItem* lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat(mn.lastTimeSeen)));

        CScript pubkey;
        pubkey =GetScriptForDestination(mn.pubkey.GetID());
        CTxDestination address1;
        ExtractDestination(pubkey, address1);
        CParlayAddress address2(address1);
        QTableWidgetItem *pubkeyItem = new QTableWidgetItem(QString::fromStdString(address2.ToString()));

        ui->tableWidgetPrimenodes->insertRow(0);
        ui->tableWidgetPrimenodes->setItem(0, 0, addressItem);
        ui->tableWidgetPrimenodes->setItem(0, 1, protocolItem);
        ui->tableWidgetPrimenodes->setItem(0, 2, statusItem);
        ui->tableWidgetPrimenodes->setItem(0, 3, activeSecondsItem);
        ui->tableWidgetPrimenodes->setItem(0, 4, lastSeenItem);
        ui->tableWidgetPrimenodes->setItem(0, 5, pubkeyItem);
    }

    ui->countLabel->setText(QString::number(ui->tableWidgetPrimenodes->rowCount()));
    ui->tableWidgetPrimenodes->setSortingEnabled(true);
}


void PrimenodeManager::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
    }
}

void PrimenodeManager::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
    }

}

void PrimenodeManager::on_createButton_clicked()
{
    AddEditAdrenalineNode* aenode = new AddEditAdrenalineNode();
    aenode->exec();
}

void PrimenodeManager::on_startButton_clicked()
{
    // start the node
    QItemSelectionModel* selectionModel = ui->tableWidget_2->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();
    if(selected.count() == 0)
        return;

    QModelIndex index = selected.at(0);
    int r = index.row();
    std::string sAlias = ui->tableWidget_2->item(r, 0)->text().toStdString();

    if(pwalletMain->IsLocked()) {
    }

    std::string statusObj;
    statusObj += "<center>Alias: " + sAlias;

    BOOST_FOREACH(CPrimenodeConfig::CPrimenodeEntry mne, primenodeConfig.getEntries()) {
        if(mne.getAlias() == sAlias) {
            std::string errorMessage;
            bool result = activePrimenode.Register(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), mne.getRewardAddress(), mne.getRewardPercentage(), errorMessage);

            if(result) {
                statusObj += "<br>Successfully started primenode." ;
            } else {
                statusObj += "<br>Failed to start primenode.<br>Error: " + errorMessage;
            }
            break;
        }
    }
    statusObj += "</center>";
    pwalletMain->Lock();

    QMessageBox msg;
    msg.setText(QString::fromStdString(statusObj));

    msg.exec();
}

void PrimenodeManager::on_startAllButton_clicked()
{
    if(pwalletMain->IsLocked()) {
    }

    std::vector<CPrimenodeConfig::CPrimenodeEntry> mnEntries;

    int total = 0;
    int successful = 0;
    int fail = 0;
    std::string statusObj;

    BOOST_FOREACH(CPrimenodeConfig::CPrimenodeEntry mne, primenodeConfig.getEntries()) {
        total++;

        std::string errorMessage;
        std::string strRewardAddress = mne.getRewardAddress();
        std::string strRewardPercentage = mne.getRewardPercentage();

        bool result = activePrimenode.Register(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strRewardAddress, strRewardPercentage, errorMessage);

        if(result) {
            successful++;
        } else {
            fail++;
            statusObj += "\nFailed to start " + mne.getAlias() + ". Error: " + errorMessage;
        }
    }
    pwalletMain->Lock();

    std::string returnObj;
    returnObj = "Successfully started " + boost::lexical_cast<std::string>(successful) + " primenodes, failed to start " +
            boost::lexical_cast<std::string>(fail) + ", total " + boost::lexical_cast<std::string>(total);
    if (fail > 0)
        returnObj += statusObj;

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();
}

void PrimenodeManager::on_UpdateButton_clicked()
{
    BOOST_FOREACH(CPrimenodeConfig::CPrimenodeEntry mne, primenodeConfig.getEntries()) {
        std::string errorMessage;
        std::string strRewardAddress = mne.getRewardAddress();
        std::string strRewardPercentage = mne.getRewardPercentage();

        std::vector<CPrimenode> vPrimenodes = mnodeman.GetFullPrimenodeVector();
        if (errorMessage == ""){
            updateAdrenalineNode(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), QString::fromStdString(mne.getPrivKey()), QString::fromStdString(mne.getTxHash()),
                QString::fromStdString(mne.getOutputIndex()), QString::fromStdString(strRewardAddress), QString::fromStdString(strRewardPercentage), QString::fromStdString("Not in the primenode list."));
        }
        else {
            updateAdrenalineNode(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), QString::fromStdString(mne.getPrivKey()), QString::fromStdString(mne.getTxHash()),
                QString::fromStdString(mne.getOutputIndex()), QString::fromStdString(strRewardAddress), QString::fromStdString(strRewardPercentage), QString::fromStdString(errorMessage));
        }

        BOOST_FOREACH(CPrimenode& mn, vPrimenodes) {
            if (mn.addr.ToString().c_str() == mne.getIp()){
                updateAdrenalineNode(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), QString::fromStdString(mne.getPrivKey()), QString::fromStdString(mne.getTxHash()),
                QString::fromStdString(mne.getOutputIndex()), QString::fromStdString(strRewardAddress), QString::fromStdString(strRewardPercentage), QString::fromStdString("Primenode is Running."));
            }
        }
    }
}

void PrimenodeManager::showContextMenu(const QPoint& point)
{
    QTableWidgetItem* item = ui->tableWidgetPrimenodes->itemAt(point);
    if (item) contextMenu->exec(QCursor::pos());
}

void PrimenodeManager::copyAddress()
{
    std::string sData;
    int row;
    QItemSelectionModel* selectionModel = ui->tableWidgetPrimenodes->selectionModel();
    QModelIndexList selectedRows = selectionModel->selectedRows();
    if(selectedRows.count() == 0)
        return;
    
    for (int i = 0; i < selectedRows.count(); i++)
    {
        QModelIndex index = selectedRows.at(i);
        row = index.row();
        sData += ui->tableWidgetPrimenodes->item(row, 0)->text().toStdString();
        if (i < selectedRows.count()-1)
            sData += "\n";
    }
    
    QApplication::clipboard()->setText(QString::fromStdString(sData));
}

void PrimenodeManager::copyPubkey()
{
    std::string sData;
    int row;
    QItemSelectionModel* selectionModel = ui->tableWidgetPrimenodes->selectionModel();
    QModelIndexList selectedRows = selectionModel->selectedRows();
    if(selectedRows.count() == 0)
        return;
    
    for (int i = 0; i < selectedRows.count(); i++)
    {
        QModelIndex index = selectedRows.at(i);
        row = index.row();
        sData += ui->tableWidgetPrimenodes->item(row, 5)->text().toStdString();
        if (i < selectedRows.count()-1)
            sData += "\n";
    }
    
    QApplication::clipboard()->setText(QString::fromStdString(sData));
}