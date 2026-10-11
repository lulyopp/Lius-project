#include "FineTuneWgt.h"
#include "ui_FineTuneWgt.h"

#include <QFont>
#include <QCursor>
#include <QHeaderView>
#include <QBrush>
#include <QMessageBox>
#include <QAbstractItemView>

FineTuneWgt::FineTuneWgt(QTableWidget* paramTable, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FineTuneWgt),
    m_paramTable(paramTable)
{
    ui->setupUi(this);

    // 与第一页（单一调整）按钮风格保持一致
    QFont labFont;
    labFont.setPointSize(16);

    const QString primaryBlueStyle = R"(
        QPushButton {
            background-color: #0078D7;
            color: white;
            border-radius: 6px;
            border: none;
            font-weight: bold;
            font-size: 14pt;
        }
        QPushButton:hover { background-color: #005A9E; }
        QPushButton:pressed { background-color: #004578; padding-top: 2px; }
        QPushButton:disabled { background-color: #a5aaa3; }
    )";

    const QString redStopStyle = R"(
        QPushButton {
            background-color: #D13438;
            color: white;
            border-radius: 6px;
            border: none;
            font-weight: bold;
            font-size: 14pt;
        }
        QPushButton:hover { background-color: #B3272B; }
        QPushButton:pressed { background-color: #8E1F22; padding-top: 2px; }
        QPushButton:disabled { background-color: #E8999B; color: #F0F0F0; }
    )";

    const QString arrowBlueStyle = R"(
        QPushButton {
            background-color: #0078D7;
            color: white;
            border-radius: 4px;
            border: none;
            font-weight: bold;
            font-size: 12pt;
            min-width: 48px;
            max-width: 60px;
            min-height: 36px;
        }
        QPushButton:hover { background-color: #005A9E; }
        QPushButton:pressed { background-color: #004578; padding-top: 2px; }
        QPushButton:disabled { background-color: #a5aaa3; }
    )";

    ui->btnSysdata->setFont(labFont);
    ui->btnSysdata->setMinimumHeight(45);
    ui->btnSysdata->setMinimumWidth(120);
    ui->btnSysdata->setCursor(Qt::PointingHandCursor);
    ui->btnSysdata->setStyleSheet(primaryBlueStyle);

    ui->btnStart->setFont(labFont);
    ui->btnStart->setMinimumHeight(45);
    ui->btnStart->setMinimumWidth(120);
    ui->btnStart->setCursor(Qt::PointingHandCursor);
    ui->btnStart->setStyleSheet(primaryBlueStyle);

    ui->btnForeSTop->setFont(labFont);
    ui->btnForeSTop->setMinimumHeight(45);
    ui->btnForeSTop->setMinimumWidth(120);
    ui->btnForeSTop->setCursor(Qt::PointingHandCursor);
    ui->btnForeSTop->setStyleSheet(redStopStyle);

    ui->spinPosStep->setMinimumHeight(30);
    ui->spinWidthStep->setMinimumHeight(30);

    ui->btnPosLeft->setStyleSheet(arrowBlueStyle);
    ui->btnPosRight->setStyleSheet(arrowBlueStyle);
    ui->btnWidthLeft->setStyleSheet(arrowBlueStyle);
    ui->btnWidthRight->setStyleSheet(arrowBlueStyle);

    // 统一调整表格样式与 m_paramTable 保持一致
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setStyleSheet(R"(
        QHeaderView::section {
            background-color: #E6F2FF;
            color: #333333;
            font-weight: bold;
            border: none;
            border-right: 1px solid #D0D0D0;
            border-bottom: 1px solid #D0D0D0;
            padding: 4px;
        }
    )");
    QFont tableFont = ui->tableWidget->font();
    tableFont.setPointSize(16);
    ui->tableWidget->setFont(tableFont);
    ui->tableWidget->horizontalHeader()->setFont(tableFont);
    ui->tableWidget->verticalHeader()->setVisible(false);
    ui->tableWidget->setAlternatingRowColors(true);
    ui->tableWidget->setStyleSheet("QTableWidget { alternate-background-color: #FFFFFF; background-color: #F8F8F8; }");
    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);

    ui->tableWidget->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(ui->tableWidget,&QTableWidget::customContextMenuRequested,
            this,&FineTuneWgt::onTableContextMenu);

}

FineTuneWgt::~FineTuneWgt()
{
    delete ui;
}

void FineTuneWgt::on_btnPosLeft_clicked()
{
    int row = ui->tableWidget->currentRow();
    if (row < 0) {
        return;
    }
    adjustParamTableValue(row, 1, ui->spinPosStep->value());
}

void FineTuneWgt::on_btnPosRight_clicked()
{
    int row = ui->tableWidget->currentRow();
    if (row < 0) {
        return;
    }
    adjustParamTableValue(row, 1, -ui->spinPosStep->value());
}

void FineTuneWgt::on_btnWidthLeft_clicked()
{
    int row = ui->tableWidget->currentRow();
    if (row < 0) {
        return;
    }
    adjustParamTableValue(row, 4, ui->spinWidthStep->value());
}

void FineTuneWgt::on_btnWidthRight_clicked()
{
    int row = ui->tableWidget->currentRow();
    if (row < 0) {
        return;
    }
    adjustParamTableValue(row, 4, -ui->spinWidthStep->value());
}

void FineTuneWgt::adjustParamTableValue(int tableRow, int paramColumn, double delta)
{
    if (!m_paramTable || tableRow < 0 || tableRow >= ui->tableWidget->rowCount()) {
        return;
    }

    QTableWidgetItem* mzItem = ui->tableWidget->item(tableRow, 0);
    if (!mzItem) {
        return;
    }

    QString mzText = mzItem->text().trimmed();
    if (mzText.isEmpty()) {
        return;
    }

    int paramRow = -1;
    for (int i = 0; i < m_paramTable->rowCount(); ++i) {
        QTableWidgetItem* item = m_paramTable->item(i, 0);
        if (item && item->text().trimmed() == mzText) {
            paramRow = i;
            break;
        }
    }

    if (paramRow < 0) {
        return;
    }

    QTableWidgetItem* paramItem = m_paramTable->item(paramRow, paramColumn);
    if (!paramItem) {
        return;
    }

    bool ok = false;
    double currentValue = paramItem->text().toDouble(&ok);
    if (!ok) {
        return;
    }

    double newValue = currentValue + delta;
    int decimals = (paramColumn == 1) ? 0 : 3;
    QString newText = QString::number(newValue, 'f', decimals);

    m_paramTable->blockSignals(true);
    paramItem->setText(newText);
    m_paramTable->blockSignals(false);
}

void FineTuneWgt::on_btnSysdata_clicked()
{
    if (!m_paramTable) {
        QMessageBox::warning(nullptr,tr("错误"),tr("参数表为空nullptr！"));
        return;
    }

    ui->tableWidget->blockSignals(true);
    ui->tableWidget->setRowCount(0);

    bool hasValid = false;
    for (int i = 0; i < m_paramTable->rowCount(); ++i) {
        QTableWidgetItem* checkItem = m_paramTable->item(i, 8);
        if (!checkItem || checkItem->checkState() != Qt::Checked) {
            continue;
        }

        QTableWidgetItem* mzItem = m_paramTable->item(i, 0);
        if (!mzItem || mzItem->text().trimmed().isEmpty()) {
            continue;
        }

        int row = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(row);
        QTableWidgetItem* mzOutItem = new QTableWidgetItem(mzItem->text().trimmed());
        mzOutItem->setForeground(QBrush(QColor(0, 150, 255)));
        ui->tableWidget->setItem(row, 0, mzOutItem);
        ui->tableWidget->setItem(row, 1, new QTableWidgetItem(""));
        ui->tableWidget->setItem(row, 2, new QTableWidgetItem(""));
        ui->tableWidget->setItem(row, 3, new QTableWidgetItem(""));

        hasValid = true;
    }

    ui->tableWidget->blockSignals(false);

    if(!hasValid){
        QMessageBox::warning(nullptr,tr("错误"),tr("无勾选的数据！"));
        return;
    }
}

void FineTuneWgt::on_btnStart_clicked()
{
    if (!m_paramTable) {
        QMessageBox::warning(nullptr, tr("错误"), tr("参数表为空nullptr！"));
        return;
    }

    QList<double> targetMasses;
    QList<int> paramTableRows;

    for (int r = 0; r < ui->tableWidget->rowCount(); ++r) {
        QTableWidgetItem* mzItem = ui->tableWidget->item(r, 0);
        if (!mzItem || mzItem->text().trimmed().isEmpty()) {
            continue;
        }

        bool ok = false;
        double mz = mzItem->text().trimmed().toDouble(&ok);
        if (!ok) {
            continue;
        }

        int paramRow = -1;
        for (int i = 0; i < m_paramTable->rowCount(); ++i) {
            QTableWidgetItem* item = m_paramTable->item(i, 0);
            if (item && qFuzzyCompare(item->text().trimmed().toDouble(), mz)) {
                paramRow = i;
                break;
            }
        }

        if (paramRow < 0) {
            continue;
        }

        targetMasses.append(mz);
        paramTableRows.append(paramRow);
    }

    if (targetMasses.isEmpty()) {
        QMessageBox::warning(nullptr, tr("错误"), tr("统一调整表格中没有有效的目标m/z！"));
        return;
    }

    onUnifiedScanStarted();
    emit requestFineTuneUnifiedScan(targetMasses, paramTableRows);
}

void FineTuneWgt::onUnifiedScanStarted()
{
    targetPosLst.clear();

    ui->btnStart->setEnabled(false);
    ui->btnStart->setText(tr("⏳ 精调中..."));
    ui->btnForeSTop->setEnabled(true);

    for (int r = 0; r < ui->tableWidget->rowCount(); ++r) {
        ui->tableWidget->setItem(r, 1, new QTableWidgetItem(""));
        ui->tableWidget->setItem(r, 2, new QTableWidgetItem(""));
        ui->tableWidget->setItem(r, 3, new QTableWidgetItem(""));
    }
}

void FineTuneWgt::onUnifiedScanFinished()
{
    ui->btnStart->setEnabled(true);
    ui->btnStart->setText(tr("开始精调"));
    ui->btnForeSTop->setEnabled(false);
}

void FineTuneWgt::updatePeakResult(int row, double intensity, double position, double width)
{
    if (row < 0 || row >= ui->tableWidget->rowCount()) {
        return;
    }
    ui->tableWidget->setItem(row, 1, new QTableWidgetItem(QString::number(intensity, 'e', 2)));
    ui->tableWidget->setItem(row, 2, new QTableWidgetItem(QString::number(position, 'f', 3)));
    ui->tableWidget->setItem(row, 3, new QTableWidgetItem(QString::number(width, 'f', 3)));
}

void FineTuneWgt::getPosAdjustData(int row, double targetPos)
{
    if(row < 0 || targetPos < 1e-5)
    {
        return;
    }
    targetPosLst.append(targetPos);
}

void FineTuneWgt::on_btnForeSTop_clicked()
{
    emit requestFineTuneUnifiedStop();
}

void FineTuneWgt::onTableContextMenu(const QPoint &pos)
{
    // pos 是相对于 QTableWidget 的局部坐标
    // 获取当前点击位置的单元格
    QTableWidgetItem *item = ui->tableWidget->itemAt(pos);

    // 只在有效单元格上显示菜单（空白处不显示）
    if (!item) {
        return;
    }

    QMenu contexMenu(this);
    QAction* posAdjust = contexMenu.addAction("峰位置目标校准");
    connect(posAdjust, &QAction::triggered, this, &FineTuneWgt::onPosAdjust);
    // 将局部坐标转为全局坐标，在鼠标位置弹出菜单
    contexMenu.exec(ui->tableWidget->viewport()->mapToGlobal(pos));
}

void FineTuneWgt::onPosAdjust()
{
    if(targetPosLst.isEmpty()){
        return;
    }
    int row = ui->tableWidget->currentRow();
    if (row < 0 || !m_paramTable) return;
    QTableWidgetItem* mzItem = ui->tableWidget->item(row, 0);
    if(!mzItem){
        return;
    }
    QString mzText = mzItem->text().trimmed();
    if(mzText.isEmpty()){
        return;
    }
    int paramRow = -1;
    for (int i = 0; i < m_paramTable->rowCount(); ++i) {
        QTableWidgetItem* item = m_paramTable->item(i, 0);
        if (item && item->text().trimmed() == mzText) {
            paramRow = i;
            break;
        }
    }
    if(paramRow < 0) return;
    QTableWidgetItem* paramItem = m_paramTable->item(paramRow, 1);
    if (!paramItem) {
        return;
    }
    if(targetPosLst.size() < row) return;
    double newPosition = targetPosLst[row];
    QString newText = QString::number(newPosition, 'f', 0);
    m_paramTable->blockSignals(true);
    paramItem->setText(newText);
    m_paramTable->blockSignals(false);
}


