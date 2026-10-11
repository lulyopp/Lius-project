#include "AutoTuningWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDateTime>
#include <QDebug>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QDir>
#include <QCoreApplication>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QTabWidget>
#include <QTimer>
#include <cmath>
#include <algorithm>
#include <limits>

#include "AutoTuningWidget.h"
#include "AutoTuningWidget.h"
#include "AutoTuningWidget.h"

AutoTuningWidget::AutoTuningWidget(HardwareInterface* hw, QWidget *parent)
    : QWidget(parent), m_hardware(hw)
{
    setupUI();
    setupThread();
    //窗口最大化
    this->setWindowState(Qt::WindowMaximized);
}

AutoTuningWidget::~AutoTuningWidget()
{
    // WA_DeleteOnClose 会自动释放，只需置空防止野指针
    if (m_workerThread && m_workerThread->isRunning()) {
        m_worker->forceStop();
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

void AutoTuningWidget::closeEvent(QCloseEvent *event)
{
    hide();
    event->ignore();
}

void AutoTuningWidget::setTuneDialogParams(const QString& title, sConfigure* configure, const QString& tuneFilePath)
{
    this->setWindowTitle(title);
    m_tuneDialogTitle = title;
    m_tuneConfigure = configure;
    m_tuneFilePath = tuneFilePath;
}

/*!
 * \brief AutoTuningWidget::setParams 电参赋值
 * \param i_ep
 * \param i_em
 */
void AutoTuningWidget::setParams(int i_ep, int i_em)
{
    m_tuneSettings.em = i_em;
    m_tuneSettings.ep = i_ep;
}

void AutoTuningWidget::showTuneDialog()
{
}

void AutoTuningWidget::setConfigFilePath(QString path)
{
    m_configFilePath = path;
    //设置好默认路径后，立刻读取默认json
    loadDefaultSettings();
    
    // 【新增】检查是否有备份文件，提示用户恢复
    QTimer::singleShot(500, this, &AutoTuningWidget::restoreFromBackup);
}

void AutoTuningWidget::onTableContextMenu(const QPoint &pos)
{
    // pos 是相对于 QTableWidget 的局部坐标
    // 获取当前点击位置的单元格
    QTableWidgetItem *item = m_paramTable->itemAt(pos);

    // 只在有效单元格上显示菜单（空白处不显示）
    if (!item) {
        return;
    }

    // 将局部坐标转为全局坐标，在鼠标位置弹出菜单
    m_contextMenu->exec(m_paramTable->mapToGlobal(pos));
}

// ========== 插入行（方案1增强版）==========
void AutoTuningWidget::onInsertRow()
{
    int row = m_paramTable->currentRow();
    if (row < 0) row = m_paramTable->rowCount() - 1; // 没选中则插到最后
    if (m_paramTable->rowCount() > 15) {
        QMessageBox::warning(this, "错误", "有效数据行数大于16行！");
        return;
    }
    // 1. 先取消现有合并（关键！）
    clearSpanForColumn(2);  // Gain
    clearSpanForColumn(3);  // Offset

    // 2. 插入新行 + 预填充所有列（你的方案1）
    int newRow = row + 1;
    m_paramTable->insertRow(newRow);
    for (int col = 0; col < m_paramTable->columnCount(); ++col) {
        m_paramTable->setItem(newRow, col, new QTableWidgetItem(""));
    }
    m_paramTable->setItem(newRow, m_paramTable->columnCount() - 1, createCheckItem());

    // 3. 重新设置合并（新总行数）
    int totalRowCount = m_paramTable->rowCount();
    m_paramTable->setSpan(0, 2, totalRowCount, 1);
    m_paramTable->setSpan(0, 3, totalRowCount, 1);

    // 4. 重新设置对齐（因为 span 重建后可能丢失）
    if (m_paramTable->item(0, 2))
        m_paramTable->item(0, 2)->setTextAlignment(Qt::AlignCenter);
    if (m_paramTable->item(0, 3))
        m_paramTable->item(0, 3)->setTextAlignment(Qt::AlignCenter);
}

// ========== 删除行 ===========
void AutoTuningWidget::onDeleteRow()
{
    int row = m_paramTable->currentRow();
    if (row < 0 || m_paramTable->rowCount() <= 1) return; // 至少保留1行

    QString globalGain = m_paramTable->item(0, 2) ? m_paramTable->item(0, 2)->text() : "";
    QString globalOffset = m_paramTable->item(0, 3) ? m_paramTable->item(0, 3)->text() : "";

    // 1. 取消合并
    clearSpanForColumn(2);
    clearSpanForColumn(3);

    // 2. 删除行（先删数据，再删行，避免悬垂指针）
    for (int col = 0; col < m_paramTable->columnCount(); ++col) {
        delete m_paramTable->takeItem(row, col);
    }
    m_paramTable->removeRow(row);

    // 3. 重新合并（如果还剩行）
    int totalRowCount = m_paramTable->rowCount();
    if (totalRowCount > 0) {
        m_paramTable->setSpan(0, 2, totalRowCount, 1);
        m_paramTable->setSpan(0, 3, totalRowCount, 1);

        // 确保 (0,2) 和 (0,3) 有 item，否则对齐设置无效
        if (!m_paramTable->item(0, 2))
        {
            m_paramTable->setItem(0, 2, new QTableWidgetItem(globalGain));
        }
        else
        {
            m_paramTable->item(0, 2)->setText(globalGain);
        }

        if (!m_paramTable->item(0, 3))
        {
            m_paramTable->setItem(0, 3, new QTableWidgetItem(globalOffset));
        }
        else
        {
            m_paramTable->item(0, 3)->setText(globalOffset);
        }

        m_paramTable->item(0, 2)->setTextAlignment(Qt::AlignCenter);
        m_paramTable->item(0, 3)->setTextAlignment(Qt::AlignCenter);
    }
}

// m/z 单元格发生变化提示必须保证顺序，否在重新输入
void AutoTuningWidget::onParamTableItemChanged(QTableWidgetItem* item)
{
    if (!item) return;
    if (item->column() != 0) return;

    int row = item->row();
    bool ok = false;
    int newMz = item->text().toInt(&ok);
    QString lastValidStr = item->data(Qt::UserRole).toString();

    if (!ok)
    {
        m_paramTable->blockSignals(true);
        item->setText(lastValidStr);
        m_paramTable->blockSignals(false);
        return;
    }

    double prevMz = -std::numeric_limits<double>::max();
    double nextMz = std::numeric_limits<double>::max();

    if (row > 0)
    {
        QTableWidgetItem* prevItem = m_paramTable->item(row - 1, 0);
        if (prevItem)
        {
            double v = prevItem->text().toDouble(&ok);
            if (ok) prevMz = v;
        }
    }

    if (row < m_paramTable->rowCount() - 1)
    {
        QTableWidgetItem* nextItem = m_paramTable->item(row + 1, 0);
        if (nextItem)
        {
            double v = nextItem->text().toDouble(&ok);
            if (ok) nextMz = v;
        }
    }

    if (newMz <= prevMz || newMz >= nextMz)
    {
        QString msg;
        if (row == 0)
            msg = QString("第一行 m/z 必须小于下一行 m/z (%1)").arg(nextMz);
        else if (row == m_paramTable->rowCount() - 1)
            msg = QString("最后一行 m/z 必须大于上一行 m/z (%1)").arg(prevMz);
        else
            msg = QString("第 %1 行 m/z 必须大于 %2 且小于 %3").arg(row + 1).arg(prevMz).arg(nextMz);

        QMessageBox::warning(this, "m/z 顺序错误", msg);

        m_paramTable->blockSignals(true);
        item->setText(lastValidStr);
        m_paramTable->blockSignals(false);
    }
    else
    {
        item->setData(Qt::UserRole, item->text());
    }
}

// 手动同步 m_paramTable 到 Dialog_tune 和精调下拉框
void AutoTuningWidget::onSyncParamTableToDialog()
{
    refreshFineTuneMzCombo();
    emit setParaRes(getTableData(), false);
}

// ========== 辅助：清除指定列的所有 span ===========
void AutoTuningWidget::clearSpanForColumn(int col)
{
    // 遍历所有行，如果该行该列有 span，则清除
    for (int row = 0; row < m_paramTable->rowCount(); ++row) {
        int rowSpan = m_paramTable->rowSpan(row, col);
        int colSpan = m_paramTable->columnSpan(row, col);
        if (rowSpan > 1 || colSpan > 1) {
            m_paramTable->setSpan(row, col, 1, 1); // 还原为 1x1
        }
    }
}

QTableWidgetItem* AutoTuningWidget::createCheckItem(bool checked)
{
    QTableWidgetItem* item = new QTableWidgetItem();
    item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    item->setTextAlignment(Qt::AlignCenter);
    return item;
}

bool AutoTuningWidget::isRowEnabled(QTableWidget* table, int row)
{
    QTableWidgetItem* item = table->item(row, table->columnCount() - 1);
    if (!item) return false;
    return item->checkState() == Qt::Checked;
}

bool AutoTuningWidget::readDoubleFromTable(QTableWidget* table, int row, int col, double& outValue, const QString& fieldName)
{
    if (!table) {
        handleLogMessage(QString("<span style='color: #D13438;'>错误：表格为空，无法读取 %1</span>").arg(fieldName));
        return false;
    }

    QTableWidgetItem* item = table->item(row, col);
    if (!item || item->text().trimmed().isEmpty()) {
        handleLogMessage(QString("<span style='color: #D13438;'>错误：%1 单元格为空 (行%2, 列%3)</span>")
                         .arg(fieldName).arg(row).arg(col));
        return false;
    }

    bool ok = false;
    double value = item->text().trimmed().toDouble(&ok);
    if (!ok) {
        handleLogMessage(QString("<span style='color: #D13438;'>错误：%1 不是有效数字：%2 (行%3, 列%4)</span>")
                         .arg(fieldName).arg(item->text()).arg(row).arg(col));
        return false;
    }

    outValue = value;
    return true;
}

void AutoTuningWidget::refreshFineTuneMzCombo()
{
    int oldRow = -1;
    if (m_cmbFineTuneMz->currentIndex() >= 0) {
        oldRow = m_cmbFineTuneMz->currentData().toInt();
    }

    m_cmbFineTuneMz->blockSignals(true);
    m_cmbFineTuneMz->clear();

    for (int i = 0; i < m_paramTable->rowCount(); ++i) {
        QTableWidgetItem* item = m_paramTable->item(i, 0);
        if (item && !item->text().trimmed().isEmpty()) {
            m_cmbFineTuneMz->addItem(item->text(), i);
        }
    }

    m_cmbFineTuneMz->blockSignals(false);

    int idx = -1;
    for (int i = 0; i < m_cmbFineTuneMz->count(); ++i) {
        if (m_cmbFineTuneMz->itemData(i).toInt() == oldRow) {
            idx = i;
            break;
        }
    }
    if (idx >= 0) {
        m_cmbFineTuneMz->setCurrentIndex(idx);
    } else if (m_cmbFineTuneMz->count() > 0) {
        m_cmbFineTuneMz->setCurrentIndex(0);
        onFineTuneTargetChanged(0);
    }
}

void AutoTuningWidget::loadFromUI()
{

}

void AutoTuningWidget::loadFromPropFile()
{
    QString defaultPath = QCoreApplication::applicationDirPath() + "\\Devices";
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open File"), defaultPath, "Prop Files(*.propFile)");
    if (fileName.isEmpty()) {
        return;
    }

    if(QMessageBox::No==QMessageBox::question(this, "提醒", "确认从propFile加载数据，当前数据会被覆盖！")){
        return;
    }

    // 1. 读取 ini 文件
    QSettings settings(fileName, QSettings::IniFormat);

    // 2. 读取 [TuneV] 节的值
    settings.beginGroup("TuneV");
    double resoGain = settings.value("resoGain", 0.500).toDouble();
    double resoOffset = settings.value("resoOffset", 0.000).toDouble();
    QString valueStr = settings.value("value").toString();
    settings.endGroup();

    if (valueStr.isEmpty()) {
        handleLogMessage(QString("<span style='color: #D13438;'>载入失败：value 字段为空</span>"));
        return;
    }

    // 3. 解析 value 字符串
    // value 格式: "col1;col2;col3;col4"  每列内部用逗号分隔行
    QStringList columns = valueStr.split(';');
    if (columns.size() < 4) {
        handleLogMessage(QString("<span style='color: #D13438;'>载入失败：value 格式错误，需要4列数据</span>"));
        return;
    }

    // 解析4列数据
    QStringList mzList = columns[0].split(',');
    QStringList posList = columns[1].split(',');
    QStringList resoList = columns[2].split(',');
    QStringList codeList = columns[3].split(',');  // 第4列 -> offset_Code

    // 4. 确定有效数据行数（取4列中最小的）
    int maxRows = qMin(qMin(mzList.size(), posList.size()),
                       qMin(resoList.size(), codeList.size()));

    // 过滤有效行：mz 不为空且不为0
    QList<int> validRows;
    for (int i = 0; i < maxRows; ++i) {
        QString mzStr = mzList[i].trimmed();
        bool ok = false;
        double mzVal = mzStr.toDouble(&ok);
        if (ok && mzVal > 0) {
            validRows.append(i);
        }
    }

    if (validRows.isEmpty()) {
        handleLogMessage(QString("<span style='color: #D13438;'>载入失败：未找到有效的 m/z 数据</span>"));
        return;
    }

    // 5. 清空现有表格并准备写入
    m_paramTable->setRowCount(0);
    m_paramTable->blockSignals(true);
    m_cmbFineTuneMz->blockSignals(true);
    m_cmbFineTuneMz->clear();

    // 先取消现有的 span
    clearSpanForColumn(2);
    clearSpanForColumn(3);

    // 6. 写入有效数据行
    int dataRowCount = validRows.size();
    if (dataRowCount > 16){
        QMessageBox::warning(this, "错误", "有效数据行数大于16！");
        return;
    }
    for (int i = 0; i < dataRowCount; ++i) {
        int srcIdx = validRows[i];
        m_paramTable->insertRow(i);

        // 第0列: m/z (蓝色)
        QString mzStr = mzList[srcIdx].trimmed();
        QTableWidgetItem* itemMz = new QTableWidgetItem(mzStr);
        itemMz->setForeground(QBrush(QColor(0, 150, 255)));
        itemMz->setData(Qt::UserRole, mzStr);
        m_paramTable->setItem(i, 0, itemMz);

        int position = posList[srcIdx].trimmed().toInt();
        // 第1列: Position
        m_paramTable->setItem(i, 1, new QTableWidgetItem(posList[srcIdx].trimmed()));

        double gain = QString::number(resoGain, 'f', 3).toDouble();
        // 第2列: Gain (只在第0行设置，后续合并单元格显示)
        if (i == 0) {
            m_paramTable->setItem(i, 2, new QTableWidgetItem(QString::number(resoGain, 'f', 3)));
        } else {
            m_paramTable->setItem(i, 2, new QTableWidgetItem(""));
        }

        // 第3列: 偏移/Offset (只在第0行设置，后续合并单元格显示)
        if (i == 0) {
            m_paramTable->setItem(i, 3, new QTableWidgetItem(QString::number(resoOffset, 'f', 3)));
        } else {
            m_paramTable->setItem(i, 3, new QTableWidgetItem(""));
        }

        // 第5列: Reso (红色)
        QString resoStr = resoList[srcIdx].trimmed();
        QTableWidgetItem* itemReso = new QTableWidgetItem(resoStr);
        itemReso->setForeground(QBrush(QColor(220, 50, 50)));
        m_paramTable->setItem(i, 5, itemReso);

        // 第7列: offset_Code (红色) <- value 第4列  DAC 与 V的计算用到这个值 - 把这一列提前赋值
        QString codeStr = codeList[srcIdx].trimmed();
        QTableWidgetItem* itemCode = new QTableWidgetItem(codeStr);
        itemCode->setForeground(QBrush(QColor(220, 50, 50)));
        m_paramTable->setItem(i, 7, itemCode);

        // 生效列：加载时默认不勾选
        m_paramTable->setItem(i, 8, createCheckItem());


        //int code = codeStr.toInt();
        int reso = resoStr.toInt();
        int code = reso - int(TuningConstants::ResoFactor * position * gain);
        double dac =(code-TuningConstants::DacOffset)/TuningConstants::DacSlope;
        double V = dac - resoOffset;

        itemCode->setText(QString::number(code));

        // 第4列: V = 0
        m_paramTable->setItem(i, 4, new QTableWidgetItem(QString::number(V,'f',5)));

        // 第6列: offset_DAC = 0 (红色)
        QTableWidgetItem* itemDac = new QTableWidgetItem(QString::number(dac,'f',5));
        itemDac->setForeground(QBrush(QColor(220, 50, 50)));
        m_paramTable->setItem(i, 6, itemDac);
    }

    // 7. 追加1行空白行作为预留
    int currentRows = m_paramTable->rowCount();
    m_paramTable->setRowCount(currentRows + 1);
    for (int i = currentRows; i < m_paramTable->rowCount(); ++i) {
        for (int c = 0; c < 9; ++c) {
            m_paramTable->setItem(i, c, new QTableWidgetItem(""));
        }
        m_paramTable->setItem(i, 8, createCheckItem());
    }

    // 8. 重新设置 Gain/Offset 列的合并单元格
    int totalRowCount = m_paramTable->rowCount();
    m_paramTable->setSpan(0, 2, totalRowCount, 1);
    m_paramTable->setSpan(0, 3, totalRowCount, 1);

    // 确保 (0,2) 和 (0,3) 有 item 并设置对齐
    if (!m_paramTable->item(0, 2))
        m_paramTable->setItem(0, 2, new QTableWidgetItem(QString::number(resoGain, 'f', 3)));
    if (!m_paramTable->item(0, 3))
        m_paramTable->setItem(0, 3, new QTableWidgetItem(QString::number(resoOffset, 'f', 3)));

    m_paramTable->item(0, 2)->setTextAlignment(Qt::AlignCenter);
    m_paramTable->item(0, 3)->setTextAlignment(Qt::AlignCenter);

    m_paramTable->blockSignals(false);
    refreshFineTuneMzCombo();

    handleLogMessage(QString("<span style='color: #107C10;'>📂 成功从 Prop 文件载入参数: %1 (共 %2 组数据)</span>")
                     .arg(fileName).arg(dataRowCount));
}

void AutoTuningWidget::saveAsPropFile()
{
    QString defaultPath = QCoreApplication::applicationDirPath() + "\\Devices";
    QString fileName= QFileDialog::getSaveFileName(this, tr("Save As File"),
                                                       defaultPath,
                                                       "Properties Files(*.propFile)");
    if (fileName.isNull())
        return;

    saveToPropFile(fileName);
}

void AutoTuningWidget::saveToPropFile(const QString &path)
{
    if(path.isEmpty())
        return;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.close();
    else
        return;

    QVariantList dataLst = getTableData();
    int count = dataLst.count();
    if(count == 0){
        QMessageBox::warning(nullptr,tr("错误"),tr("没有需要保存的数据！"));
        return;
    }
    //取offset gain
    QVariantMap dataMap = dataLst.value(0).toMap();

    QSettings configIni(path, QSettings::IniFormat);
    configIni.setIniCodec("utf-8");

    QString str= dataMap["offset"].toString();
    configIni.setValue("/TuneV/resoOffset", str);
    str= dataMap["gain"].toString();
    configIni.setValue("/TuneV/resoGain", str);

    QStringList strList0, strList1, strList2, strList3;
    for (int i= 0; i < count; i++) {
        QVariantMap rowMap = dataLst.value(i).toMap();
        strList0<< rowMap["mz"].toString();
        strList1<< rowMap["position"].toString();
        strList2<< rowMap["reso"].toString();
        strList3<< rowMap["code"].toString();
    }
    str= strList0.join(",")+ ";"+
            strList1.join(",")+ ";"+
            strList2.join(",")+ ";"+
            strList3.join(",");
    configIni.setValue("/TuneV/value", str);

    configIni.sync();

    return ;
}

/*!
 * \brief AutoTuningWidget::autoCalParams 自动计算
 */
void AutoTuningWidget::autoCalParams()
{
    //就是校正对话框的自动计算
    QVariantList dataLst = getTableData();
    for (int i=0;i < dataLst.count();i++) {
        QVariantMap rowMap = dataLst.value(i).toMap();
        int position = rowMap["position"].toInt();
        double gain = rowMap["gain"].toDouble();
        double offset = rowMap["offset"].toDouble();
        double v = rowMap["v"].toDouble();

        rowMap["code"] = static_cast<int>((offset + v) * TuningConstants::DacSlope + TuningConstants::DacOffset);
        rowMap["reso"] = static_cast<int>(TuningConstants::ResoFactor * position * gain + ((offset + v) * TuningConstants::DacSlope + TuningConstants::DacOffset));
        rowMap["dac"] = offset + v;

        //刷新值
        QTableWidgetItem* item = m_paramTable->item(i,7);
        if(item != nullptr){
            //实际逻辑这个肯定不会空，判不判问题不大
            item->setText(rowMap["code"].toString());
        }
        item = m_paramTable->item(i,6);
        item->setText(rowMap["dac"].toString());

        item = m_paramTable->item(i,5);
        item->setText(rowMap["reso"].toString());
    }
}

void AutoTuningWidget::allSelect()
{
    if(!m_paramTable) return;
    int checkCol = m_paramTable->columnCount() - 1;
    for(int row = 0; row < m_paramTable->rowCount(); ++row){
        QTableWidgetItem* item = m_paramTable->item(row, checkCol);
        if(item && !m_paramTable->item(row, 0)->text().isEmpty()){
            item->setCheckState(Qt::Checked);
        }
    }
}

void AutoTuningWidget::allCancel()
{
    if(!m_paramTable) return;
    int checkCol = m_paramTable->columnCount() - 1;
    for(int row = 0; row < m_paramTable->rowCount(); ++row){
        QTableWidgetItem* item = m_paramTable->item(row, checkCol);
        if(item){
            item->setCheckState(Qt::Unchecked);
        }
    }
}

void AutoTuningWidget::setupUI()
{
    QFont AxisFont, LabFont;
    AxisFont.setPointSize(18);
    LabFont.setPointSize(16);
    // 顶部菜单栏 / 工具栏
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    //创建并配置顶部菜单栏
    QMenuBar* menuBar = new QMenuBar(this);
    QFont menuFont = menuBar->font();
    menuFont.setPointSize(14);
    menuBar->setFont(menuFont);
    menuBar->setStyleSheet(R"(
                           QMenuBar {
                           background-color: #F8F9FA;
                           border-bottom: 1px solid #D0D0D0;
                           }
                           QMenuBar::item {
                           padding: 8px 16px;
                           background: transparent;
                           color: #333333;
                           }
                           QMenuBar::item:selected {
                           background-color: #E0EEF9;
                           color: #0078D7;
                           }
                           QMenuBar::item:pressed {
                           background-color: #CCE4F7;
                           }
                           )");
    QAction* actSettings = menuBar->addAction("⚙️ 自动调谐参数设置");
    connect(actSettings, &QAction::triggered, this, &AutoTuningWidget::onSettingsClicked);
    QAction* actTuneDialog = menuBar->addAction("🔧 查看校准参数");
    connect(actTuneDialog, &QAction::triggered, this, &AutoTuningWidget::showTuneDialog);

    mainLayout->setMenuBar(menuBar);
    QVBoxLayout* contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(9, 9, 9, 9);

    // ================== 上半部分 ==================
    QHBoxLayout* topLayout = new QHBoxLayout();

    // 左侧全局图
    m_customPlot = new QCustomPlot(this);
    m_customPlot->addGraph();
    m_customPlot->graph(0)->setPen(QPen(Qt::blue, 2));
    m_customPlot->xAxis->setLabel(" AMU ");
    m_customPlot->yAxis->setLabel(" A ");
    m_customPlot->xAxis->setLabelFont(AxisFont);
    m_customPlot->yAxis->setLabelFont(AxisFont);
    m_customPlot->xAxis->setRange(0, 60);
    m_customPlot->yAxis->setRange(0, 1.0e-10);
    m_customPlot->yAxis->setNumberFormat("e");
    m_customPlot->yAxis->setNumberPrecision(1);
    m_customPlot->xAxis->setTickLabelFont(AxisFont);
    m_customPlot->yAxis->setTickLabelFont(AxisFont);
    m_customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    connect(m_customPlot, &QCustomPlot::mouseDoubleClick, this, &AutoTuningWidget::onGlobalPlotMousePress);
    topLayout->addWidget(m_customPlot, 2);

    // --- 坐标追踪器初始化 ---
    m_tracer = new QCPItemTracer(m_customPlot);
    m_tracer->setGraph(m_customPlot->graph(0));
    m_tracer->setInterpolating(true);
    m_tracer->setStyle(QCPItemTracer::tsCircle);
    m_tracer->setPen(QPen(Qt::red));
    m_tracer->setBrush(Qt::red);
    m_tracer->setSize(7);
    m_tracer->setVisible(false);
    m_coordLabel = new QCPItemText(m_customPlot);
    m_coordLabel->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
    m_coordLabel->position->setType(QCPItemPosition::ptPlotCoords);
    m_coordLabel->setPen(QPen(Qt::black));
    m_coordLabel->setBrush(QBrush(QColor(255, 255, 255, 180)));
    m_coordLabel->setFont(AxisFont);
    m_coordLabel->setVisible(false);
    connect(m_customPlot, &QCustomPlot::mousePress, this, &AutoTuningWidget::onMousePress);

    // 右侧精调面板
    QTabWidget* fineTuneTab = new QTabWidget();
    QFont fineTuneFont = LabFont;
    fineTuneFont.setBold(true);
    fineTuneTab->setFont(fineTuneFont);
    fineTuneTab->setStyleSheet(R"(
                          QTabBar::tab {
                          font-size: 16pt;
                          font-weight: bold;
                          padding: 10px 30px; /* 增加标签的宽高 */
                          background: #E1DFDD;
                          border-top-left-radius: 4px;
                          border-top-right-radius: 4px;
                          }
                          QTabBar::tab:selected {
                          background: #FFFFFF;
                          color: #0078D7;
                          border-bottom: 2px solid #0078D7;
                          }
                          )");
    QGroupBox* groupFineTune = new QGroupBox(this);
    groupFineTune->setFont(LabFont);
    QVBoxLayout* vTuning = new QVBoxLayout(groupFineTune);
    QLabel* lblTitle = new QLabel("🎛 精确参数调试", this);
    QFont titleFont = LabFont;
    titleFont.setBold(true);
    lblTitle->setFont(titleFont);
    vTuning->addWidget(lblTitle);

    m_localPlot = new QCustomPlot(this);
    m_localPlot->addGraph();
    m_localPlot->graph(0)->setPen(QPen(Qt::blue, 2));
    m_localPlot->xAxis->setTickLabelFont(AxisFont);
    m_localPlot->yAxis->setVisible(false);

    m_localPlot->xAxis->grid()->setSubGridVisible(true);
    QPen majorGridPen(QColor(0, 0, 0), 1, Qt::SolidLine);
    m_localPlot->xAxis->grid()->setPen(majorGridPen);
    QPen minorGridPen(QColor(225, 0, 0), 1, Qt::DotLine);
    m_localPlot->xAxis->grid()->setSubGridPen(minorGridPen);
    m_localPlot->xAxis->grid()->setLayer("grid");

    vTuning->addWidget(m_localPlot, 1);
    QHBoxLayout* hMz = new QHBoxLayout();
    hMz->addWidget(new QLabel("目标 M/Z:"));
    m_cmbFineTuneMz = new QComboBox();
    hMz->addWidget(m_cmbFineTuneMz);

    vTuning->addLayout(hMz);
    m_lblPeakHeight = new QLabel("峰  强: -");
    vTuning->addWidget(m_lblPeakHeight);
    //property动态属性-----"t"表示目标，"s"表示步长
    //-------------------------pos
    QHBoxLayout* hPos = new QHBoxLayout();
    hPos->addWidget(new QLabel("峰位置:"));
    QPushButton* btnPosLL = new QPushButton("<<"); btnPosLL->setProperty("s", -2); btnPosLL->setProperty("t", "pos");
    QPushButton* btnPosL  = new QPushButton("<"); btnPosL->setProperty("s", -1); btnPosL->setProperty("t", "pos");
    //在中间加入一个垂直布局，包含数值和拉杆
    QVBoxLayout* vCenterPos = new QVBoxLayout();
    m_lblCurrentPos = new QLabel("-");
    m_lblCurrentPos->setAlignment(Qt::AlignCenter);
    m_lblCurrentPos->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    m_sliderPos = new QSlider(Qt::Horizontal);
    m_sliderPos->setRange(-100, 100);   //最大步长10 * 10次 = 100
    m_sliderPos->setValue(0);
    m_sliderPos->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_sliderPos->setFocusPolicy(Qt::NoFocus);
    m_sliderPos->setTickPosition(QSlider::TicksBelow);
    m_sliderPos->setTickInterval(10);

    //在拉杆下添加刻度标签
    QHBoxLayout* hPosLabels = new QHBoxLayout();
    QLabel* lblPosMin = new QLabel("min"); lblPosMin->setStyleSheet("color: #888888; font-size: 11px");
    QLabel* lblPosZero = new QLabel("0"); lblPosZero->setStyleSheet("color: #0078D7; font-size: 12px; font-weight: bold");
    QLabel* lblPosMax = new QLabel("max"); lblPosMax->setStyleSheet("color: #888888; font-size: 11px");
    hPosLabels->addWidget(lblPosMin, 0, Qt::AlignLeft);
    hPosLabels->addWidget(lblPosZero, 0, Qt::AlignHCenter);
    hPosLabels->addWidget(lblPosMax, 0, Qt::AlignRight);
    hPosLabels->setContentsMargins(2, 0, 2, 0);
    vCenterPos->addWidget(m_lblCurrentPos);
    vCenterPos->addWidget(m_sliderPos);
    vCenterPos->addLayout(hPosLabels);

    QPushButton* btnPosR  = new QPushButton(">"); btnPosR->setProperty("s", 1); btnPosR->setProperty("t", "pos");
    QPushButton* btnPosRR = new QPushButton(">>"); btnPosRR->setProperty("s", 2); btnPosRR->setProperty("t", "pos");
    hPos->addWidget(btnPosLL); hPos->addWidget(btnPosL);
    hPos->addLayout(vCenterPos, 1);
    hPos->addWidget(btnPosR); hPos->addWidget(btnPosRR);
    vTuning->addLayout(hPos);
    //----------------------------------V
    QHBoxLayout* hV = new QHBoxLayout();
    hV->addWidget(new QLabel("峰  宽:"));
    QPushButton* btnVLL = new QPushButton("<<"); btnVLL->setProperty("s", -2); btnVLL->setProperty("t", "v");
    QPushButton* btnVL  = new QPushButton("<");  btnVL->setProperty("s", -1);  btnVL->setProperty("t", "v");
    //在中间加入一个垂直布局，包含数值和拉杆
    QVBoxLayout* vCenterV = new QVBoxLayout();
    m_lblCurrentV = new QLabel("-");
    m_lblCurrentV->setAlignment(Qt::AlignCenter);
    m_lblCurrentV->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    m_sliderV = new QSlider(Qt::Horizontal);
    m_sliderV->setRange(-100, 100);
    m_sliderV->setValue(0);
    m_sliderV->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_sliderV->setFocusPolicy(Qt::NoFocus);
    m_sliderV->setTickPosition(QSlider::TicksBelow);
    m_sliderV->setTickInterval(10);

    //在拉杆下添加刻度标签
    QHBoxLayout* hVLabels = new QHBoxLayout();
    QLabel* lblVMin = new QLabel("min"); lblVMin->setStyleSheet("color: #888888; font-size: 11px");
    QLabel* lblVZero = new QLabel("0"); lblVZero->setStyleSheet("color: #0078D7; font-size: 12px; font-weight: bold");
    QLabel* lblVMax = new QLabel("max"); lblVMax->setStyleSheet("color: #888888; font-size: 11px");
    hVLabels->addWidget(lblVMin, 0, Qt::AlignLeft);
    hVLabels->addWidget(lblVZero, 0, Qt::AlignHCenter);
    hVLabels->addWidget(lblVMax, 0, Qt::AlignRight);
    hVLabels->setContentsMargins(2, 0, 2, 0);
    vCenterV->addWidget(m_lblCurrentV);
    vCenterV->addWidget(m_sliderV);
    vCenterV->addLayout(hVLabels);
    QPushButton* btnVR  = new QPushButton(">");  btnVR->setProperty("s", 1);   btnVR->setProperty("t", "v");
    QPushButton* btnVRR = new QPushButton(">>"); btnVRR->setProperty("s", 2);  btnVRR->setProperty("t", "v");
    hV->addWidget(btnVLL); hV->addWidget(btnVL);
    hV->addLayout(vCenterV, 1);
    hV->addWidget(btnVR); hV->addWidget(btnVRR);
    vTuning->addLayout(hV);

    // 开始精调与停止按钮
    QHBoxLayout* hFineTuneBtns = new QHBoxLayout();

    m_btnStartFineTune = new QPushButton("▶ 开始精调", this);
    m_btnStartFineTune->setFont(LabFont);
    m_btnStartFineTune->setMinimumHeight(35);
    m_btnStartFineTune->setCursor(Qt::PointingHandCursor);
    m_btnStartFineTune->setStyleSheet(R"(
                                      QPushButton {
                                      background-color: #0078D7; color: white; border-radius: 6px; font-weight: bold; font-size: 14pt;
                                      }
                                      QPushButton:hover { background-color: #005A9E; }
                                      QPushButton:pressed { background-color: #004578; padding-top: 2px; }
                                      QPushButton:disabled {background-color: #a5aaa3; /* 禁用状态变灰 */}
                                      )");
    // 停止精调按钮
    m_btnStopFineTune = new QPushButton("🛑 停止", this);
    m_btnStopFineTune->setFont(LabFont);
    m_btnStopFineTune->setMinimumHeight(35);
    m_btnStopFineTune->setCursor(Qt::PointingHandCursor);
    m_btnStopFineTune->setEnabled(false);
    m_btnStopFineTune->setStyleSheet(R"(
                                     QPushButton { background-color: #D13438; color: white; border-radius: 6px; font-weight: bold; font-size: 14pt; }
                                     QPushButton:hover { background-color: #B3272B; }
                                     QPushButton:pressed { background-color: #8E1F22; padding-top: 2px; }
                                     QPushButton:disabled { background-color: #E8999B; color: #F0F0F0; }
                                     )");
    hFineTuneBtns->addWidget(m_btnStartFineTune, 2);
    hFineTuneBtns->addWidget(m_btnStopFineTune, 1);
    vTuning->addSpacing(10);
    vTuning->addLayout(hFineTuneBtns);
    connect(m_btnStartFineTune, &QPushButton::clicked, this, &AutoTuningWidget::onStartFineTuneClicked);
    connect(m_btnStopFineTune, &QPushButton::clicked, this, &AutoTuningWidget::onStopFineTuneClicked);

    fineTuneTab->addTab(groupFineTune,tr("单一调整"));

    topLayout->addWidget(fineTuneTab, 1);
    mainLayout->addLayout(topLayout, 4);

    // --- 下方：三栏水平布局 ---
    QTabWidget* tabWgt = new QTabWidget();
    QFont tabFont = LabFont;
    tabFont.setBold(true);
    tabWgt->setFont(tabFont);
    tabWgt->setStyleSheet(R"(
                          QTabBar::tab {
                          font-size: 16pt;
                          font-weight: bold;
                          padding: 10px 30px; /* 增加标签的宽高 */
                          background: #E1DFDD;
                          border-top-left-radius: 4px;
                          border-top-right-radius: 4px;
                          }
                          QTabBar::tab:selected {
                          background: #FFFFFF;
                          color: #0078D7;
                          border-bottom: 2px solid #0078D7;
                          }
                          )");
    QWidget* tabAutoWgt = new QWidget();//自动调谐 - 即原先的
    QWidget* tabElWgt = new QWidget();//拓展参数

    QHBoxLayout* bottomLayout = new QHBoxLayout();

    // 1. 左侧：表格+按钮
    QVBoxLayout* leftColumn = new QVBoxLayout();

    QHBoxLayout* tableTitleLayout = new QHBoxLayout();
    QLabel* lblTableTitle = new QLabel("📊 仪器控制参数表", this);
    lblTableTitle->setFont(titleFont);
    tableTitleLayout->addWidget(lblTableTitle, 0, Qt::AlignVCenter);
    tableTitleLayout->addStretch();
    QFont TBtnFont;
    TBtnFont.setPointSize(13);
    m_btnLoadPropFile = new QPushButton("从PropFile加载");
    m_btnLoadPropFile->setFont(TBtnFont);
    m_btnLoadPropFile->setMinimumHeight(35);
    m_btnLoadPropFile->setMinimumWidth(95);
    m_btnSavePropFile = new QPushButton("另存为PropFile");
    m_btnSavePropFile->setFont(TBtnFont);
    m_btnSavePropFile->setMinimumHeight(35);
    m_btnSavePropFile->setMinimumWidth(95);
    m_btnLoadTable = new QPushButton("📂 载入", this);
    m_btnLoadTable->setFont(TBtnFont);
    m_btnSaveTable = new QPushButton("💾 保存", this);
    m_btnSaveTable->setFont(TBtnFont);
    m_btnLoadTable->setMinimumHeight(35);
    m_btnLoadTable->setMinimumWidth(95);
    m_btnSaveTable->setMinimumHeight(35);
    m_btnSaveTable->setMinimumWidth(95);
    m_btnLoadPropFile->setCursor(Qt::PointingHandCursor);
    m_btnSavePropFile->setCursor(Qt::PointingHandCursor);
    m_btnLoadTable->setCursor(Qt::PointingHandCursor);
    m_btnSaveTable->setCursor(Qt::PointingHandCursor);
    tableTitleLayout->addWidget(m_btnLoadPropFile);
    tableTitleLayout->addWidget(m_btnSavePropFile);
    tableTitleLayout->addStretch();
    tableTitleLayout->addWidget(m_btnLoadTable);
    tableTitleLayout->addWidget(m_btnSaveTable);
    leftColumn->addLayout(tableTitleLayout);
    // 绑定信号槽
    connect(m_btnLoadPropFile, &QPushButton::clicked, this, &AutoTuningWidget::loadFromPropFile);
    connect(m_btnSavePropFile, &QPushButton::clicked, this, &AutoTuningWidget::saveAsPropFile);
    /*
    connect(m_btnSavePropFile, &QPushButton::clicked, this, [=]() {
        //this->hide();
        emit savePropFile();
    });
    */
    connect(m_btnLoadTable, &QPushButton::clicked, this, &AutoTuningWidget::onLoadTableClicked);
    connect(m_btnSaveTable, &QPushButton::clicked, this, &AutoTuningWidget::onSaveTableClicked);

    int defaultDataCount = 3;   //初始化填入数据3行
    int totalRowCount = 16;     //预留表格行
    m_paramTable = new QTableWidget(totalRowCount, 9, this);
    connect(m_paramTable,&QTableWidget::customContextMenuRequested,
            this,&AutoTuningWidget::onTableContextMenu);
    connect(m_paramTable, &QTableWidget::itemChanged,
            this, &AutoTuningWidget::onParamTableItemChanged);
    m_paramTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_paramTable->setHorizontalHeaderLabels({"m/z", "Position", "Gain", "偏移", "V", "Reso", "DAC", "Code", "生效"});
    m_paramTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_paramTable->horizontalHeader()->setStyleSheet(R"(
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
    QFont font = m_paramTable->font();
    font.setPointSize(14);
    m_paramTable->setFont(font);
    m_paramTable->horizontalHeader()->setFont(font);
    m_paramTable->verticalHeader()->setVisible(false);
    m_paramTable->setAlternatingRowColors(true);
    m_paramTable->setStyleSheet("QTableWidget { alternate-background-color: #FFFFFF; background-color: #F8F8F8; }");
    m_paramTable->blockSignals(true);

    QStringList mzs = {"18", "28", "40"};
    QStringList positions = {"3693", "5797", "8301"};
    QStringList gains = {"0.223", "0.223", "0.223"};
    QStringList offsets = {"0.00", "0.00", "0.00"};
    QStringList Vs = {"0.000", "0.000", "0.000"};
    QStringList Resos = {"33229", "33676", "34067"};
    QStringList DACs = {"0.0", "0.0", "0.0"};
    QStringList Codes = {"32768", "32768", "32768"};

    for(int i=0; i<defaultDataCount; ++i)
    {
        QTableWidgetItem* itemMz = new QTableWidgetItem(mzs[i]);
        itemMz->setForeground(QBrush(QColor(0, 150, 255)));
        itemMz->setData(Qt::UserRole, mzs[i]);
        m_paramTable->setItem(i, 0, itemMz);

        m_paramTable->setItem(i, 1, new QTableWidgetItem(positions[i]));
        m_paramTable->setItem(i, 2, new QTableWidgetItem(gains[i]));
        m_paramTable->setItem(i, 3, new QTableWidgetItem(offsets[i]));
        m_paramTable->setItem(i, 4, new QTableWidgetItem(Vs[i]));

        QTableWidgetItem* itemReso = new QTableWidgetItem(Resos[i]);
        itemReso->setForeground(QBrush(QColor(220, 50, 50)));
        m_paramTable->setItem(i, 5, itemReso);

        QTableWidgetItem* itemDAC = new QTableWidgetItem(DACs[i]);
        itemDAC->setForeground(QBrush(QColor(220, 50, 50)));
        m_paramTable->setItem(i, 6, itemDAC);

        QTableWidgetItem* itemCode = new QTableWidgetItem(Codes[i]);
        itemCode->setForeground(QBrush(QColor(220, 50, 50)));
        m_paramTable->setItem(i, 7, itemCode);

        m_paramTable->setItem(i, 8, createCheckItem());
    }
    // 预留表格为空白
    for(int i = defaultDataCount; i < defaultDataCount + 1; ++i)
    {
        for(int c = 0; c < 9; ++c)
        {
            m_paramTable->setItem(i, c, new QTableWidgetItem(""));
        }
        m_paramTable->setItem(i, 8, createCheckItem());
    }
    // 将 Gain 和 Offset 列分别统一为1行
    m_paramTable->setSpan(0, 2, totalRowCount, 1);
    m_paramTable->setSpan(0, 3, totalRowCount, 1);
    m_paramTable->item(0, 2)->setTextAlignment(Qt::AlignCenter);
    m_paramTable->item(0, 3)->setTextAlignment(Qt::AlignCenter);
    m_paramTable->blockSignals(false);

    leftColumn->addWidget(m_paramTable,1);

    //统一调整 -- 因为需要m_paramTable，就放到m_paramTable初始化后面
    m_fineTuneWgt = new FineTuneWgt(m_paramTable);
    fineTuneTab->addTab(m_fineTuneWgt,tr("统一调整"));

    connect(m_fineTuneWgt, &FineTuneWgt::requestFineTuneUnifiedScan,
            this, &AutoTuningWidget::onStartFineTuneUnifiedScan);
    connect(m_fineTuneWgt, &FineTuneWgt::requestFineTuneUnifiedStop,
            this, &AutoTuningWidget::onStopFineTuneUnifiedScan);

    //调试、停止、设置按钮
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_btnTuneGain = new QPushButton("⏩调节Gain", this);
    m_btnTuneOffset = new QPushButton("⏩调节偏移", this);
    m_btnTunePosition = new QPushButton("⏩调节Position", this);
    m_btnTuneGain->setMinimumHeight(35);
    m_btnTuneOffset->setMinimumHeight(35);
    m_btnTunePosition->setMinimumHeight(35);
    m_btnTuneGain->setFont(LabFont);
    m_btnTuneOffset->setFont(LabFont);
    m_btnTunePosition->setFont(LabFont);
    QString primaryButtonStyle = R"(
                                 QPushButton {
                                 background-color: #0078D7; /* 浅蓝 */
                                 color: white;
                                 border-radius: 6px; /* 圆角 */
                                 border: none;
                                 font-weight: bold;
                                 }
                                 QPushButton:hover {
                                 background-color: #005A9E; /* 鼠标悬浮变深 */
                                 }
                                 QPushButton:pressed {
                                 background-color: #004578; /* 点击时更深 */
                                 padding-top: 2px; /* 产生按压凹陷感 */
                                 }
                                 QPushButton:disabled {
                                 background-color: #a5aaa3; /* 禁用状态变灰 */
                                 }
                                 )";
    m_btnTuneGain->setStyleSheet(primaryButtonStyle);
    m_btnTuneOffset->setStyleSheet(primaryButtonStyle);
    m_btnTunePosition->setStyleSheet(primaryButtonStyle);
    m_btnTuneGain->setCursor(Qt::PointingHandCursor);
    m_btnTuneOffset->setCursor(Qt::PointingHandCursor);
    m_btnTunePosition->setCursor(Qt::PointingHandCursor);
    connect(m_btnTuneGain, &QPushButton::clicked, this, &AutoTuningWidget::onTuneGainClicked);
    connect(m_btnTuneOffset, &QPushButton::clicked, this, &AutoTuningWidget::onTuneOffsetClicked);
    connect(m_btnTunePosition, &QPushButton::clicked, this, &AutoTuningWidget::onTunePositionClicked);

    m_btnStopDebug = new QPushButton("🛑 停止 ", this);
    m_btnStopDebug->setMinimumHeight(35);
    m_btnStopDebug->setFont(LabFont);
    m_btnStopDebug->setEnabled(false);
    m_btnStopDebug->setStyleSheet(R"(
                                  QPushButton {
                                  background-color: #D13438;
                                  color: white;
                                  border-radius: 6px;
                                  border: none;
                                  font-weight: bold;
                                  }
                                  QPushButton:hover { background-color: #B3272B; }
                                  QPushButton:pressed { background-color: #8E1F22; padding-top: 2px; }
                                  QPushButton:disabled { background-color: #E8999B; color: #F0F0F0; }
                                  )");
    connect(m_btnStopDebug, &QPushButton::clicked, this, &AutoTuningWidget::onStopClicked);
    QString toolButtonStyle = R"(
                              QPushButton {
                              background-color: #FFFFFF;
                              color: #333333;
                              border: 1px solid #CCCCCC;
                              border-radius: 4px;
                              font-weight: bold;
                              }
                              QPushButton:hover {
                              background-color: #F0F8FF;
                              border: 1px solid #0078D7;
                              color: #0078D7;
                              }
                              QPushButton:pressed {
                              background-color: #E0EEF9;
                              padding-top: 2px;
                              }
                              )";
    btnLayout->addWidget(m_btnTuneGain);
    btnLayout->addWidget(m_btnTuneOffset);
    btnLayout->addWidget(m_btnTunePosition);
    btnLayout->addWidget(m_btnStopDebug);

    leftColumn->addLayout(btnLayout);
    tabAutoWgt->setLayout(leftColumn);
    tabWgt->addTab(tabAutoWgt,"Auto");

    //初始化tabElWgt
    {
        // 主布局：垂直布局
        QVBoxLayout* elMainLayout = new QVBoxLayout(tabElWgt);
        elMainLayout->setContentsMargins(9, 9, 9, 9);
        elMainLayout->setSpacing(10);

        // Adv 表格顶部标题与载入/保存按钮
        QHBoxLayout* elTableTitleLayout = new QHBoxLayout();
        QLabel* lblElTableTitle = new QLabel("📊 拓展调谐控制参数表 (Adv)", this);
        lblElTableTitle->setFont(titleFont); // 复用上方定义的 titleFont
        elTableTitleLayout->addWidget(lblElTableTitle, 0, Qt::AlignVCenter);
        elTableTitleLayout->addStretch();

        m_btnLoadAdvTable = new QPushButton("📂 载入", tabElWgt);
        m_btnSaveAdvTable = new QPushButton("💾 保存", tabElWgt);

        m_btnLoadAdvTable->setFont(TBtnFont); // 复用上方定义的 TBtnFont
        m_btnSaveAdvTable->setFont(TBtnFont);
        m_btnLoadAdvTable->setMinimumHeight(35);
        m_btnLoadAdvTable->setMinimumWidth(95);
        m_btnSaveAdvTable->setMinimumHeight(35);
        m_btnSaveAdvTable->setMinimumWidth(95);
        m_btnLoadAdvTable->setCursor(Qt::PointingHandCursor);
        m_btnSaveAdvTable->setCursor(Qt::PointingHandCursor);

        elTableTitleLayout->addWidget(m_btnLoadAdvTable);
        elTableTitleLayout->addWidget(m_btnSaveAdvTable);
        elMainLayout->addLayout(elTableTitleLayout);

        // 绑定专属的信号槽
        connect(m_btnLoadAdvTable, &QPushButton::clicked, this, &AutoTuningWidget::onLoadAdvTableClicked);
        connect(m_btnSaveAdvTable, &QPushButton::clicked, this, &AutoTuningWidget::onSaveAdvTableClicked);
        // -------------------------------

        // --- 上半部分：QTableWidget ---
        // 与 m_paramTable 相同的初始化参数
        int defaultDataCount = 3;   // 初始化填入数据3行
        int totalRowCount = 16;     // 预留表格行

        // 9列：m_paramTable 的 8 列 + DestInten
        m_elTable = new QTableWidget(totalRowCount, 10, tabElWgt);
        connect(m_elTable,&QTableWidget::customContextMenuRequested,
                this,&AutoTuningWidget::onEleTableContextMenu);
        m_elTable->setContextMenuPolicy(Qt::CustomContextMenu);
        m_elTable->setHorizontalHeaderLabels({"m/z", "Position", "Gain", "偏移", "V", "Reso", "DAC", "Code", "DestInten", "生效"});
        m_elTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_elTable->horizontalHeader()->setStyleSheet(R"(
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

        QFont elFont = m_elTable->font();
        elFont.setPointSize(13);
        m_elTable->setFont(elFont);
        m_elTable->horizontalHeader()->setFont(elFont);
        m_elTable->verticalHeader()->setVisible(false);
        m_elTable->setAlternatingRowColors(true);
        m_elTable->setStyleSheet("QTableWidget { alternate-background-color: #FFFFFF; background-color: #F8F8F8; }");

        // 与 m_paramTable 完全相同的初始数据 + DestIntens
        QStringList DestIntens = {"0", "0", "0"};  // DestInten 初始值

        for(int i = 0; i < defaultDataCount; ++i)
        {
            QTableWidgetItem* itemMz = new QTableWidgetItem(mzs[i]);
            itemMz->setForeground(QBrush(QColor(0, 150, 255)));

            m_elTable->setItem(i, 0, itemMz);
            m_elTable->setItem(i, 1, new QTableWidgetItem(positions[i]));

            // Gain 和 Offset 设置为居中，保持视觉统一
            QTableWidgetItem* itemGain = new QTableWidgetItem(gains[i]);
            itemGain->setTextAlignment(Qt::AlignCenter);
            m_elTable->setItem(i, 2, itemGain);

            QTableWidgetItem* itemOffset = new QTableWidgetItem(offsets[i]);
            itemOffset->setTextAlignment(Qt::AlignCenter);
            m_elTable->setItem(i, 3, itemOffset);

            m_elTable->setItem(i, 4, new QTableWidgetItem(Vs[i]));

            QTableWidgetItem* itemReso = new QTableWidgetItem(Resos[i]);
            itemReso->setForeground(QBrush(QColor(220, 50, 50)));
            m_elTable->setItem(i, 5, itemReso);

            QTableWidgetItem* itemDAC = new QTableWidgetItem(DACs[i]);
            itemDAC->setForeground(QBrush(QColor(220, 50, 50)));
            m_elTable->setItem(i, 6, itemDAC);

            QTableWidgetItem* itemCode = new QTableWidgetItem(Codes[i]);
            itemCode->setForeground(QBrush(QColor(220, 50, 50)));
            m_elTable->setItem(i, 7, itemCode);

            // 新增列 DestInten
            m_elTable->setItem(i, 8, new QTableWidgetItem(DestIntens[i]));

            m_elTable->setItem(i, 9, createCheckItem());
        }

        // 预留表格为空白
        for(int i = defaultDataCount; i < defaultDataCount + 1; ++i)
        {
            for(int c = 0; c < 10; ++c)
            {
                m_elTable->setItem(i, c, new QTableWidgetItem(""));
            }
            m_elTable->setItem(i, 9, createCheckItem());
        }

        // 注意：Gain(col 2) 和 偏移(col 3) 不合并单元格
        // m_paramTable 中有 setSpan(0, 2, totalRowCount, 1) 和 setSpan(0, 3, totalRowCount, 1)
        // 这里明确不调用 setSpan，保持每行独立

        elMainLayout->addWidget(m_elTable, 1);

        // --- 下半部分：按钮行 ---
        QHBoxLayout* elBtnLayout = new QHBoxLayout();
        elBtnLayout->setSpacing(15);

        m_btnSyncParam = new QPushButton("手动同步参数", tabElWgt);
        m_btnStartExec = new QPushButton("开始执行", tabElWgt);
        m_btnForceStop = new QPushButton("强制停止", tabElWgt);

        // 按钮样式与现有按钮风格保持一致
        m_btnSyncParam->setMinimumHeight(35);
        m_btnStartExec->setMinimumHeight(35);
        m_btnForceStop->setMinimumHeight(35);

        QFont btnFont = LabFont;  // 复用 setupUI 中定义的 LabFont
        m_btnSyncParam->setFont(btnFont);
        m_btnStartExec->setFont(btnFont);
        m_btnForceStop->setFont(btnFont);

        // 开始执行按钮使用主按钮蓝色风格
        m_btnSyncParam->setStyleSheet(primaryButtonStyle);
        m_btnStartExec->setStyleSheet(primaryButtonStyle);
        m_btnForceStop->setEnabled(false);
        // 强制停止按钮使用红色警示风格
        m_btnForceStop->setStyleSheet(R"(
                                      QPushButton {
                                      background-color: #D13438;
                                      color: white;
                                      border-radius: 6px;
                                      border: none;
                                      font-weight: bold;
                                      }
                                      QPushButton:hover { background-color: #B3272B; }
                                      QPushButton:pressed { background-color: #8E1F22; padding-top: 2px; }
                                      QPushButton:disabled { background-color: #E8999B; color: #F0F0F0; }
                                      )");

        m_btnSyncParam->setCursor(Qt::PointingHandCursor);
        m_btnStartExec->setCursor(Qt::PointingHandCursor);
        m_btnForceStop->setCursor(Qt::PointingHandCursor);

        elBtnLayout->addStretch();
        elBtnLayout->addWidget(m_btnSyncParam);
        elBtnLayout->addWidget(m_btnStartExec);
        elBtnLayout->addWidget(m_btnForceStop);
        elBtnLayout->addStretch();

        elMainLayout->addLayout(elBtnLayout);

        // 信号槽连接（请根据实际需求在头文件中声明并实现槽函数）
        connect(m_btnSyncParam, &QPushButton::clicked, this, &AutoTuningWidget::onSyncParamClicked);
        connect(m_btnStartExec, &QPushButton::clicked, this, &AutoTuningWidget::onStartExecClicked);
        connect(m_btnForceStop, &QPushButton::clicked, this, &AutoTuningWidget::onForceStopClicked);
    }

    tabWgt->addTab(tabElWgt,"Adv");
    bottomLayout->addWidget(tabWgt,3);
    //bottomLayout->addLayout(leftColumn, 3);

    // 2. 中间：拟合图
    QVBoxLayout* fitLayout = new QVBoxLayout();

    QHBoxLayout* fitTitleLayout = new QHBoxLayout();

    QLabel* lblFitTitle = new QLabel("📈 position - m/z拟合曲线", this);
    lblFitTitle->setFont(titleFont);
    fitTitleLayout->addWidget(lblFitTitle, 0, Qt::AlignLeft);
    fitTitleLayout->addStretch();

    QPushButton* btnFit = new QPushButton("勾选拟合");
    btnFit->setFont(TBtnFont);
    btnFit->setMinimumHeight(35);
    btnFit->setCursor(Qt::PointingHandCursor);
    fitTitleLayout->addWidget(btnFit, 0, Qt::AlignRight);
    fitTitleLayout->addStretch();
    connect(btnFit, &QPushButton::clicked, this, [this](){
        QVector<double> xData, yData;
        int rowCount = m_paramTable->rowCount();
        for(int i = 0; i < rowCount; ++i){
            if(!isRowEnabled(m_paramTable, i)) continue;
            QTableWidgetItem* itemMz = m_paramTable->item(i, 0);
            QTableWidgetItem* itemPos = m_paramTable->item(i, 1);
            if(!itemMz || !itemPos || itemMz->text().trimmed().isEmpty() || itemMz->text().trimmed().isEmpty()) continue;
            bool ok1, ok2;
            double mz = itemMz->text().toDouble(&ok1);
            double pos = itemPos->text().toDouble(&ok2);
            if(ok1 && ok2){
                xData.append(mz);
                yData.append(pos);
            }
        }

        if(xData.size() < 2){
            QMessageBox::warning(this, "拟合失败", "勾选的有效数据点不足2个，无法进行线性拟合！");
            return;
        }

        // 最小二乘法
        double sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
        int n = xData.size();
        for(int i = 0; i < n; ++i){
            sumX += xData[i];
            sumY += yData[i];
            sumXY += xData[i] * yData[i];
            sumXX += xData[i] * xData[i];
        }
        double meanX = sumX / n;
        double meanY = sumY / n;
        double denominator = (sumXX - sumX * sumX / n);
        double k = 0, b = meanY, rSquared = 0;
        if(std::fabs(denominator) != 0){
            k = (sumXY - sumX * sumY / n) / denominator;
            b = meanY - k * meanX;

            double ssTot = 0, ssRes = 0;
            for(int i = 0; i < n; ++i){
                double fi = k * xData[i] + b;
                ssTot += (yData[i] - meanY) * (yData[i] - meanY);
                ssRes += (yData[i] - fi) * (yData[i] - fi);
            }
            if(std::fabs(ssTot) != 0){
                rSquared = 1.0 - (ssRes / ssTot);
            }
            else{
                rSquared = 1.0;
            }
        }
        handleTrendlineReady(k, b, rSquared, xData, yData);
    });

    fitTitleLayout->addSpacerItem(new QSpacerItem(0, 10, QSizePolicy::Minimum, QSizePolicy::Fixed));
    fitLayout->addLayout(fitTitleLayout);

    m_fitPlot = new QCustomPlot(this);
    m_fitPlot->addGraph();
    m_fitPlot->graph(0)->setLineStyle(QCPGraph::lsNone);
    m_fitPlot->graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 8));
    m_fitPlot->addGraph();
    m_fitPlot->graph(1)->setPen(QPen(Qt::red, 2));
    m_fitPlot->xAxis->setLabel("m/z");
    m_fitPlot->yAxis->setLabel("Position");
    m_fitPlot->xAxis->setLabelFont(AxisFont);
    m_fitPlot->yAxis->setLabelFont(AxisFont);
    m_fitPlot->xAxis->setTickLabelFont(AxisFont);
    m_fitPlot->yAxis->setTickLabelFont(AxisFont);

    fitLayout->addWidget(m_fitPlot,1);
    bottomLayout->addLayout(fitLayout, 2);

    // 3. 右侧：日志
    QVBoxLayout* logLayout = new QVBoxLayout();
    QHBoxLayout* logTitleLayout = new QHBoxLayout();
    logTitleLayout->setAlignment(Qt::AlignVCenter);
    QLabel* lblLogTitle = new QLabel("🧾 运行日志", this);
    lblLogTitle->setFont(titleFont);
    logTitleLayout->addWidget(lblLogTitle, 0, Qt::AlignVCenter);
    logTitleLayout->addStretch();
    m_btnSaveLog = new QPushButton("💾 保存", this);
    m_btnSaveLog->setFont(TBtnFont);
    m_btnSaveLog->setMinimumHeight(35);
    m_btnSaveLog->setMinimumWidth(95);
    m_btnSaveLog->setCursor(Qt::PointingHandCursor);
    logTitleLayout->addWidget(m_btnSaveLog, 0, Qt::AlignVCenter);

    // 把标题水平布局加入到外层垂直布局中
    logLayout->addLayout(logTitleLayout);

    // 绑定信号槽
    connect(m_btnSaveLog, &QPushButton::clicked, this, &AutoTuningWidget::onSaveLogClicked);

    m_logTextEdit = new QTextEdit(this);
    QFont Messfont;
    Messfont.setPointSize(11);
    m_logTextEdit->setFont(Messfont);
    m_logTextEdit->setReadOnly(true);
    m_logTextEdit->setLineWrapMode(QTextEdit::NoWrap);
    m_logTextEdit->setStyleSheet("QTextEdit { background-color: #F8F9FA; border: 1px solid #D0D0D0; border-radius: 4px; }");
    logLayout->addWidget(m_logTextEdit,1);
    bottomLayout->addLayout(logLayout, 2);

    mainLayout->addLayout(bottomLayout, 3);

    //状态提示栏【显示日志等级高的 、关键信息】
    {
        QWidget* statusWgt = new QWidget();
        QHBoxLayout* sLayout = new QHBoxLayout();
        statusWgt->setLayout(sLayout);
        m_lbInfo = new QLabel();

        QLabel* lb = new QLabel(tr("关键信息："));
        lb->setStyleSheet("QLabel { color: blue; font-size: 16px; font-weight: bold; }");
        sLayout->addWidget(lb);
        sLayout->addWidget(m_lbInfo);

        mainLayout->addWidget(statusWgt);
    }

    // 精调事件绑定
    connect(m_cmbFineTuneMz, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AutoTuningWidget::onFineTuneTargetChanged);
    // 精调按键存入容器
    QList<QPushButton*> fns = {btnPosLL, btnPosL, btnPosR, btnPosRR, btnVLL, btnVL, btnVR, btnVRR};
    for(auto b : fns) connect(b, &QPushButton::clicked, this, &AutoTuningWidget::onFineTuneAdjustClicked);
    for(auto b : fns) {
        b->setStyleSheet(toolButtonStyle);
        b->setMinimumHeight(35);
        b->setMinimumWidth(35);
        b->setCursor(Qt::PointingHandCursor); // 鼠标悬浮变成小手
    }
    m_btnStopDebug->setCursor(Qt::PointingHandCursor);

    // 初始化完成时，自动将表格数据同步到精调面板
    refreshFineTuneMzCombo();

    //表格右键菜单
    initTableMenu();

    //adv电参表所用的参数表右键菜单
    initEleTableMenu();
}

void AutoTuningWidget::setupThread()
{
    qRegisterMetaType<QVector<double>>("QVector<double>");
    qRegisterMetaType<QList<double>>("QList<double>");
    qRegisterMetaType<InstrumentParams>("InstrumentParams");
    qRegisterMetaType<QList<InstrumentParams>>("QList<InstrumentParams>");//方便参数传输
    // 注册 QVector<PeakResult> 跨线程元类型
    qRegisterMetaType<QVector<PeakResult>>("QVector<PeakResult>");

    if(m_workerThread && m_workerThread->isRunning()) return;     //若子线程正在运行，返回

    m_workerThread = new QThread(this);
    m_worker = new AutoTuningWorker(m_hardware);
    m_worker->moveToThread(m_workerThread);

    connect(m_worker, &AutoTuningWorker::logMessage, this, &AutoTuningWidget::handleLogMessage);
    connect(m_worker, &AutoTuningWorker::plotDataReady, this, &AutoTuningWidget::handlePlotDataReady);
    connect(m_worker, &AutoTuningWorker::tableUpdated, this, &AutoTuningWidget::handleTableUpdate);
    connect(m_worker, &AutoTuningWorker::trendlineReady, this, &AutoTuningWidget::handleTrendlineReady);
    connect(m_worker, &AutoTuningWorker::fineTuneResultReady, this, &AutoTuningWidget::handleFineTuneResult);
    connect(m_worker, &AutoTuningWorker::fineTuneUnifiedResultReady, this, &AutoTuningWidget::handleFineTuneUnifiedResult);
    connect(m_worker, &AutoTuningWorker::workFinished, this, &AutoTuningWidget::handleWorkFinished);
    connect(m_worker, &AutoTuningWorker::sChangeParam, this, &AutoTuningWidget::handleChangeParam);
    connect(m_worker, &AutoTuningWorker::sElecParams, this, &AutoTuningWidget::handleElecParam);
    connect(m_worker, &AutoTuningWorker::sChangeAdvParam, this, &AutoTuningWidget::handleChangeAdvParam);
    connect(m_worker, &AutoTuningWorker::advTableUpdated, this, &AutoTuningWidget::handleAdvTableUpdate);
    // 连接自动调谐多峰结果信号
    connect(m_worker, &AutoTuningWorker::autoTunePeaksReady, this, &AutoTuningWidget::handleAutoTunePeaksReady);
    // 【新增】连接自动备份信号
    connect(m_worker, &AutoTuningWorker::autoBackupParams, this, &AutoTuningWidget::handleAutoBackupParams);
    // 捕获当前的指针，当线程彻底结束后，删除它们并把主界面的指针置空，
    QThread* currentThread = m_workerThread;
    AutoTuningWorker* currentWorker = m_worker;

    connect(currentThread, &QThread::finished, this, [this, currentThread, currentWorker]() {
        // 1. 安全释放对象内存
        currentWorker->deleteLater();
        currentThread->deleteLater();

        // 2. 如果主界面的指针还指着刚才死掉的线程，立刻置空！
        if (m_workerThread == currentThread) {
            m_workerThread = nullptr;
        }
        if (m_worker == currentWorker) {
            m_worker = nullptr;
        }
    });
    //
    m_workerThread->start();
}

void AutoTuningWidget::initTableMenu()
{
    m_contextMenu = new QMenu(this);
    m_actDelete = m_contextMenu->addAction("🗑️ 删除当前行");
    m_contextMenu->addSeparator();
    m_actInsert = m_contextMenu->addAction("➕ 在下方插入行");
    m_contextMenu->addSeparator();
    //m_actLoadFromUI = m_contextMenu->addAction("从已有表格加载");
    //m_contextMenu->addSeparator();
    m_actLoadPropFile = m_contextMenu->addAction("从PropFile加载");
    m_contextMenu->addSeparator();
    m_actAutoCal = m_contextMenu->addAction("自动计算");
    m_contextMenu->addSeparator();
    m_actSyncToDialog = m_contextMenu->addAction("同步到主对话框");
    m_contextMenu->addSeparator();
    m_actAllSelect = m_contextMenu->addAction("一键全选");
    m_contextMenu->addSeparator();
    m_actAllCancel = m_contextMenu->addAction("取消全选");

    // 连接菜单动作
    connect(m_actDelete, &QAction::triggered, this, &AutoTuningWidget::onDeleteRow);
    connect(m_actInsert, &QAction::triggered, this, &AutoTuningWidget::onInsertRow);
    //connect(m_actLoadFromUI, &QAction::triggered, this, &AutoTuningWidget::loadFromUI);
    connect(m_actLoadPropFile, &QAction::triggered, this, &AutoTuningWidget::loadFromPropFile);
    connect(m_actAutoCal, &QAction::triggered, this, &AutoTuningWidget::autoCalParams);
    connect(m_actSyncToDialog, &QAction::triggered, this, &AutoTuningWidget::onSyncParamTableToDialog);
    connect(m_actAllSelect, &QAction::triggered, this, &AutoTuningWidget::allSelect);
    connect(m_actAllCancel, &QAction::triggered, this, &AutoTuningWidget::allCancel);
}

/*!
 * \brief AutoTuningWidget::initEleTableMenu 电参所有的参数表-拓展功能
 */
void AutoTuningWidget::initEleTableMenu()
{
    m_contextEleMenu = new QMenu(this);
    m_actEleDelete = m_contextEleMenu->addAction("🗑️ 删除当前行");
    m_contextEleMenu->addSeparator();
    m_actEleInsert = m_contextEleMenu->addAction("➕ 在下方插入行");
    m_contextEleMenu->addSeparator();

    // 连接菜单动作
    connect(m_actEleDelete, &QAction::triggered, this, &AutoTuningWidget::onDeleteEleRow);
    connect(m_actEleInsert, &QAction::triggered, this, &AutoTuningWidget::onInsertEleRow);
}

/*!
 * \brief AutoTuningWidget::findCheckedValidIndex 找寻chk的实际index
 * \param mz double是因为最初的传入写了double，实际应该是int
 * \param i_tableType 0 表示基础表  1 表示 adv表
 * \return
 */
int AutoTuningWidget::findCheckedValidIndex(const double &mz, int i_tableType)
{
    int index = -1;
    //只获取check
    QVariantList dataLst;
    if(i_tableType == 0){
        dataLst = getTableData();
    }
    else if(i_tableType == 1){
        dataLst = getTableEleData();
    }

    for (int i =0;i < dataLst.count();i++) {
        QVariantMap rowMap = dataLst.value(i).toMap();
        double tmpMz = rowMap["mz"].toDouble();
        if(qAbs(tmpMz - mz) <= 1e-10){
            index = i;
            break;
        }
    }

    return index;
}

//3个参数调试独立按钮
void AutoTuningWidget::onTuneGainClicked()     { startSpecificTuning(0); }
void AutoTuningWidget::onTuneOffsetClicked()   { startSpecificTuning(1); }
void AutoTuningWidget::onTunePositionClicked() { startSpecificTuning(2); }

void AutoTuningWidget::startSpecificTuning(int tuneState)
{
    // 开始自动调参前，k，b，R归零
    m_currentK = 0;
    m_currentB = 0;
    m_currentR = 0;

    // 禁用所有调节按钮，启用停止按钮
    m_btnTuneGain->setEnabled(false);
    m_btnTuneOffset->setEnabled(false);
    m_btnTunePosition->setEnabled(false);
    m_btnStartFineTune->setEnabled(false);
    m_btnStopDebug->setEnabled(true);

    if (tuneState == 0) {
        m_btnTuneGain->setText("⏳ Gain 调节中...");
    } else if (tuneState == 1) {
        m_btnTuneOffset->setText("⏳ 偏移调节中...");
    } else if (tuneState == 2) {
        m_btnTunePosition->setText("⏳ Pos 调节中...");
    }

    m_hardware->startInstrument();  //重置获取谱图的终止标志
    int endMz = getMaxMz();
    emit sSetScanRange(0,endMz);    //设定初始扫描区域

    if(!m_workerThread) //若子线程未启动，则初始化子线程
    {
        setupThread();
    }

    QString tuneName = (tuneState == 0) ? "Gain" : (tuneState == 1 ? "偏移" : "Position");
    handleLogMessage(QString("已读取初始参数，准备调节 [%1]...").arg(tuneName));

    // 全局 Gain/Offset 只读一次（列 2/3 已合并单元格）
    double globalGain = 0.0, globalOffset = 0.0;
    if (!readDoubleFromTable(m_paramTable, 0, 2, globalGain, "全局 Gain") ||
        !readDoubleFromTable(m_paramTable, 0, 3, globalOffset, "全局 Offset")) {
        handleLogMessage(QString("<span style='color: #D13438;'>[%1] 调谐启动失败：全局 Gain/Offset 读取异常</span>").arg(tuneName));
        m_btnTuneGain->setEnabled(true);
        m_btnTuneOffset->setEnabled(true);
        m_btnTunePosition->setEnabled(true);
        m_btnStartFineTune->setEnabled(true);
        m_btnStopDebug->setEnabled(false);
        m_btnTuneGain->setText("⏩调节Gain");
        m_btnTuneOffset->setText("⏩调节偏移");
        m_btnTunePosition->setText("⏩调节Position");
        return;
    }

    QList<double> targetMasses;
    QList<InstrumentParams> initialParams;
    for (int row = 0; row < m_paramTable->rowCount(); ++row)
    {
        QTableWidgetItem* itemMz = m_paramTable->item(row, 0);
        // 过滤空行
        if (!itemMz || itemMz->text().trimmed().isEmpty()) continue;
        // 过滤未勾选的行
        if (!isRowEnabled(m_paramTable, row)) continue;

        bool mzOk = false;
        double currentMz = itemMz->text().trimmed().toDouble(&mzOk);
        if (!mzOk) {
            handleLogMessage(QString("<span style='color: #D13438;'>[%1] 跳过第 %2 行：m/z 不是有效数字</span>")
                             .arg(tuneName).arg(row));
            continue;
        }
        targetMasses.append(currentMz);

        InstrumentParams p;
        double position = 0.0, v = 0.0;
        readDoubleFromTable(m_paramTable, row, 1, position, QString("Position(行%1)").arg(row));
        readDoubleFromTable(m_paramTable, row, 4, v, QString("V(行%1)").arg(row));
        p.position = position;
        p.gain = globalGain;
        p.offset = globalOffset;
        p.V = v;

        initialParams.append(p);
    }

    if (targetMasses.isEmpty()) {
        handleLogMessage(QString("<span style='color: #D13438;'>[%1] 调谐启动失败：没有有效的勾选行</span>").arg(tuneName));
        m_btnTuneGain->setEnabled(true);
        m_btnTuneOffset->setEnabled(true);
        m_btnTunePosition->setEnabled(true);
        m_btnStartFineTune->setEnabled(true);
        m_btnStopDebug->setEnabled(false);
        m_btnTuneGain->setText("⏩调节Gain");
        m_btnTuneOffset->setText("⏩调节偏移");
        m_btnTunePosition->setText("⏩调节Position");
        return;
    }

    QMetaObject::invokeMethod(m_worker, "doAutoTune", Qt::QueuedConnection,
                              Q_ARG(QList<double>, targetMasses),
                              Q_ARG(QList<InstrumentParams>, initialParams),
                              Q_ARG(TuneSettings, m_tuneSettings),
                              Q_ARG(int, tuneState));
}

void AutoTuningWidget::onStopClicked() {
    handleLogMessage("=== 收到强制停止指令，正在安全退出线程... ===");
    if(m_worker)
    {
        m_worker->forceStop();
    }
    m_btnStopDebug->setEnabled(false);
    // 使停止按钮变为“过渡态”
    m_btnStopDebug->setText("⏳ 正在停止...");
    m_hardware->stopInstrument();       //打断获取数据的死循环
    emit controlScan(false);            //停止扫描
    // ⚠️ 【硬件交互】: 调谐结束关闭 V5 阀门
    m_hardware->setValveEnabled(false);
}

//鼠标显示坐标
void AutoTuningWidget::onMousePress(QMouseEvent *event)
{
    // 1. 只有左键点击才处理
    if (event->button() != Qt::LeftButton) return;

    // 2. 命中测试：判断点击位置到 graph(0) 的像素距离
    // 第二个参数为 false 表示不需要点击在数据点上，点击在连线上也算
    double distance = m_customPlot->graph(0)->selectTest(event->pos(), false);

    // 3. 判定逻辑：像素距离小于 10 则视为点击了曲线，否则视为点击空白
    if (distance >= 0 && distance < 10)
    {
        // --- 命中曲线：显示坐标 ---
        double xCoord = m_customPlot->xAxis->pixelToCoord(event->pos().x());

        // 更新 Tracer 位置并吸附
        m_tracer->setGraphKey(xCoord);
        m_tracer->updatePosition();
        double peakY = m_tracer->position->value();

        // 更新标签文本与位置
        m_coordLabel->setText(QString("m/z: %1\nInt: %2")
                              .arg(xCoord, 0, 'f', 3)
                              .arg(peakY, 0, 'e', 2));
        m_coordLabel->position->setCoords(xCoord, peakY);

        m_tracer->setVisible(true);
        m_coordLabel->setVisible(true);
    }
    else
    {
        // --- 点击空白区域：全部消失 ---
        m_tracer->setVisible(false);
        m_coordLabel->setVisible(false);
    }

    // 刷新显示
    m_customPlot->replot();
}

void AutoTuningWidget::onGlobalPlotMousePress(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton)
    {
        if(m_customPlot->graphCount() == 0 || m_customPlot->graph(0)->data()->isEmpty())
        {
            return;
        }
        m_customPlot->graph(0)->rescaleAxes();
        m_customPlot->yAxis->scaleRange(1.1, m_customPlot->yAxis->range().center());
        m_customPlot->replot();
        event->accept();
    }
}

// 变量设置弹窗
void AutoTuningWidget::onSettingsClicked() {
    QFont Wfont;
    Wfont.setPointSize(16);
    QDialog dialog(this);
    dialog.setWindowTitle("⚙ 自动调谐参数设置");
    dialog.setMinimumWidth(650);
    dialog.setStyleSheet("QDialog { background-color: #F8F9FA; }");

    QVBoxLayout* mainLayout = new QVBoxLayout(&dialog);

    QTabWidget* tabWidget = new QTabWidget(&dialog);
    tabWidget->setFont(Wfont);

    // Lambda 辅助函数，用来调整行布局
    auto createRow = [&Wfont](QWidget* parent, const QString& prefixText,
            QWidget* spinBox, const QString& suffixText) -> QHBoxLayout*
    {
        QHBoxLayout* hLayout = new QHBoxLayout();
        hLayout->setContentsMargins(10, 5, 10, 5); // 设置行的上下左右间距

        // 1. 前置文字
        QLabel* lblPrefix = new QLabel(prefixText, parent);
        lblPrefix->setFont(Wfont);
        hLayout->addWidget(lblPrefix);

        // 2. 纯数字输入框
        spinBox->setFont(Wfont);
        spinBox->setMinimumWidth(100);
        spinBox->setMaximumWidth(120);
        spinBox->setStyleSheet("QSpinBox, QDoubleSpinBox { border: 1px solid #CCCCCC; border-radius: 4px; padding: 2px 4px; background: white; }");
        hLayout->addWidget(spinBox);

        // 3. 后置文字
        if (!suffixText.isEmpty()) {
            QLabel* lblSuffix = new QLabel(suffixText, parent);
            lblSuffix->setFont(Wfont);
            hLayout->addWidget(lblSuffix);
        }

        // 4. 尾部弹簧
        hLayout->addStretch();

        return hLayout;
    };
    // ------------基础参数
    QWidget* basicTab = new QWidget();
    QVBoxLayout* basicLayout = new QVBoxLayout(basicTab);
    basicLayout->setAlignment(Qt::AlignTop);
    basicLayout->addSpacing(10);
    // 1. 最大迭代次数
    QSpinBox* spIter = new QSpinBox();
    spIter->setRange(1, 100);
    spIter->setValue(m_tuneSettings.maxIterations);
    // 2. RSD 目标容差
    QDoubleSpinBox* spRsd = new QDoubleSpinBox();
    spRsd->setRange(0.1, 100.0);
    spRsd->setSingleStep(1);
    spRsd->setDecimals(2);
    spRsd->setValue(m_tuneSettings.rsdTarget);
    // 3. offset调试的峰宽下限
    QDoubleSpinBox* spLower = new QDoubleSpinBox();
    spLower->setRange(0.1, 10.0);
    spLower->setSingleStep(0.1);
    spLower->setValue(m_tuneSettings.peakWidthLower);
    // 4. offset调试的峰宽上限
    QDoubleSpinBox* spUpper = new QDoubleSpinBox();
    spUpper->setRange(0.1, 10.0);
    spUpper->setSingleStep(0.1);
    spUpper->setValue(m_tuneSettings.peakWidthUpper);
    // 5.综合循环次数
    QSpinBox* spScanCount = new QSpinBox();
    spScanCount->setRange(1, 10);
    spScanCount->setValue(m_tuneSettings.scanCountPerEval);
    // 6.每循环平均取图次数
    QSpinBox* spMaxLength = new QSpinBox();
    spMaxLength->setRange(1, 10);
    spMaxLength->setValue(m_tuneSettings.averageMaxLength);
    // 7.自动调谐峰宽阈值
    QDoubleSpinBox* spRatio = new QDoubleSpinBox();
    spRatio->setRange(1,100);
    spRatio->setSingleStep(1);
    spRatio->setValue(m_tuneSettings.peakWidthRatio * 100);
    // 8.自动调谐峰宽阈值
    QDoubleSpinBox* spRatio_1 = new QDoubleSpinBox();
    spRatio_1->setRange(1,100);
    spRatio_1->setSingleStep(1);
    spRatio_1->setValue(m_tuneSettings.peakWidthRatio_1 * 100);
    // 9.拟合曲线要求
    QCheckBox* chkCrv = new QCheckBox(tr(""));
    chkCrv->setChecked(m_tuneSettings.positionCrvEnabled);
    QDoubleSpinBox* spCrv_R2 = new QDoubleSpinBox();
    spCrv_R2->setRange(1,100);
    spCrv_R2->setSingleStep(1);
    spCrv_R2->setValue(m_tuneSettings.positionCrv_R2 * 100);

    basicLayout->addLayout(createRow(basicTab, "最大迭代循环次数:", spIter, "次"));
    basicLayout->addLayout(createRow(basicTab, "目标 RSD:", spRsd, " %"));
    basicLayout->addLayout(createRow(basicTab, "达标峰宽下限:", spLower, ""));
    basicLayout->addLayout(createRow(basicTab, "达标峰宽上限:", spUpper, ""));
    basicLayout->addLayout(createRow(basicTab, "综合循环次数:",spScanCount, "次"));
    basicLayout->addLayout(createRow(basicTab, "每循环取图次数:",spMaxLength, "次"));
    basicLayout->addLayout(createRow(basicTab, "自动调参峰宽阈值:", spRatio, " %"));
    basicLayout->addLayout(createRow(basicTab, "精确调参峰宽阈值:", spRatio_1, " %"));

    basicLayout->addLayout(createRow(basicTab, "position校准强制执行:", chkCrv, ""));
    basicLayout->addLayout(createRow(basicTab, "拟合曲线R2阈值:", spCrv_R2, " %"));

    // ------------高级参数
    QWidget* advTab = new QWidget();
    QVBoxLayout* advLayout = new QVBoxLayout(advTab);
    advLayout->setAlignment(Qt::AlignTop);
    advLayout->addSpacing(10);

    // 9. gain步长
    QDoubleSpinBox* spGainStep = new QDoubleSpinBox();
    spGainStep->setRange(0.001, 1.0);
    spGainStep->setDecimals(3);
    spGainStep->setSingleStep(0.001);
    spGainStep->setValue(m_tuneSettings.gainStep);
    // 10. offset步长
    QDoubleSpinBox* spOffsetStep = new QDoubleSpinBox();
    spOffsetStep->setRange(0.001, 1.0);
    spOffsetStep->setDecimals(3);
    spOffsetStep->setSingleStep(0.001);
    spOffsetStep->setValue(m_tuneSettings.offsetStep);
    // 11.最小峰高阈值
    QSpinBox* spMinHeightExp = new QSpinBox();
    spMinHeightExp->setRange(-20, -1);
    spMinHeightExp->setValue(std::log10(m_tuneSettings.minPeakHeight)); // 提取指数
    // 12.峰位最大偏移量
    QDoubleSpinBox* spWindow = new QDoubleSpinBox();
    spWindow->setRange(0.0, 10.0);
    spWindow->setSingleStep(0.5);
    spWindow->setValue(m_tuneSettings.peakSearchWindow);
    // 13.质量轴局部拉伸系数stretch（形变率）下限
    QDoubleSpinBox* spStretchLower = new QDoubleSpinBox();
    spStretchLower->setRange(0.1, 2.0);
    spStretchLower->setSingleStep(0.05);
    spStretchLower->setValue(m_tuneSettings.stretchLower);
    // 14.质量轴局部拉伸系数stretch（形变率）上限
    QDoubleSpinBox* spStretchUpper = new QDoubleSpinBox();
    spStretchUpper->setRange(0.1, 2.0);
    spStretchUpper->setSingleStep(0.05);
    spStretchUpper->setValue(m_tuneSettings.stretchUpper);
    // 15.距离惩罚权重系数
    QDoubleSpinBox* spDistPenalty = new QDoubleSpinBox();
    spDistPenalty->setRange(1.0, 100.0);
    spDistPenalty->setSingleStep(1.0);
    spDistPenalty->setValue(m_tuneSettings.distancePenaltyWeight);
    // 16.精调pos小步长
    QDoubleSpinBox* spFinePosStep_1 = new QDoubleSpinBox();
    spFinePosStep_1->setRange(1, 100);
    spFinePosStep_1->setSingleStep(1);
    spFinePosStep_1->setValue(m_tuneSettings.fineTunePosStep1);
    // 17.精调pos大步长
    QDoubleSpinBox* spFinePosStep_2 = new QDoubleSpinBox();
    spFinePosStep_2->setRange(1, 100);
    spFinePosStep_2->setSingleStep(1);
    spFinePosStep_2->setValue(m_tuneSettings.fineTunePosStep2);
    // 18.精调V小步长
    QDoubleSpinBox* spFineVStep_1 = new QDoubleSpinBox();
    spFineVStep_1->setRange(0.001, 0.1);
    spFineVStep_1->setDecimals(3);
    spFineVStep_1->setSingleStep(0.001);
    spFineVStep_1->setValue(m_tuneSettings.fineTuneVStep1);
    // 19.精调V大步长
    QDoubleSpinBox* spFineVStep_2 = new QDoubleSpinBox();
    spFineVStep_2->setRange(0.001, 0.1);
    spFineVStep_2->setDecimals(3);
    spFineVStep_2->setSingleStep(0.001);
    spFineVStep_2->setValue(m_tuneSettings.fineTuneVStep2);

    advLayout->addLayout(createRow(advTab, "Gain 调节步长:", spGainStep, ""));
    advLayout->addLayout(createRow(advTab, "Offset 调节步长:", spOffsetStep, ""));
    advLayout->addLayout(createRow(advTab, "最小峰高阈值(基线噪声): 10·e", spMinHeightExp, ""));
    advLayout->addLayout(createRow(advTab, "峰位最大偏移量:", spWindow, ""));
    advLayout->addLayout(createRow(advTab, "局部拉伸系数(形变率)下限:", spStretchLower, ""));
    advLayout->addLayout(createRow(advTab, "局部拉伸系数(形变率)上限:", spStretchUpper, ""));
    advLayout->addLayout(createRow(advTab, "距离惩罚衰减权重系数:", spDistPenalty, ""));
    advLayout->addLayout(createRow(advTab, "精调 Pos 小步长:", spFinePosStep_1, ""));
    advLayout->addLayout(createRow(advTab, "精调 Pos 大步长:", spFinePosStep_2, ""));
    advLayout->addLayout(createRow(advTab, "精调 V 小步长:", spFineVStep_1, ""));
    advLayout->addLayout(createRow(advTab, "精调 V 大步长:", spFineVStep_2, ""));

    // 新增一个tab
    QWidget* elTab = new QWidget();
    QVBoxLayout* elLayout = new QVBoxLayout(elTab);
    elLayout->setAlignment(Qt::AlignTop);
    elLayout->addSpacing(10);
    // 20. 电参R²目标值
    QDoubleSpinBox* spElecR2Target = new QDoubleSpinBox();
    spElecR2Target->setRange(0.9, 0.99999);
    spElecR2Target->setDecimals(4);
    spElecR2Target->setSingleStep(0.0001);
    spElecR2Target->setValue(m_tuneSettings.elecR2Target);

    // 21. 电参R²最低阈值
    QDoubleSpinBox* spElecR2Min = new QDoubleSpinBox();
    spElecR2Min->setRange(0.5, 0.99);
    spElecR2Min->setDecimals(2);
    spElecR2Min->setSingleStep(0.01);
    spElecR2Min->setValue(m_tuneSettings.elecR2Min);

    // 22. icon 步长
    QSpinBox* spElecIconStep = new QSpinBox();
    spElecIconStep->setRange(1, 10);
    spElecIconStep->setSingleStep(1);
    spElecIconStep->setValue(m_tuneSettings.elecIconStep);

    // 23. mcp 步长
    QSpinBox* spElecMcpStep = new QSpinBox();
    spElecMcpStep->setRange(1, 10);
    spElecMcpStep->setSingleStep(1);
    spElecMcpStep->setValue(m_tuneSettings.elecMcpStep);

    // 24. 最大调节次数
    QSpinBox* spElecMaxCount = new QSpinBox();
    spElecMaxCount->setRange(1, 10);
    spElecMaxCount->setValue(m_tuneSettings.elecMaxCount);

    // 25. 小m/z阈值
    QDoubleSpinBox* spElecSmallMz = new QDoubleSpinBox();
    spElecSmallMz->setRange(1.0, 100.0);
    spElecSmallMz->setDecimals(1);
    spElecSmallMz->setSingleStep(1.0);
    spElecSmallMz->setValue(m_tuneSettings.elecSmallMzThreshold);

    //26. 拓展调谐Gain步长
    QDoubleSpinBox* spElecGainStep = new QDoubleSpinBox();
    spElecGainStep->setRange(0.001, 1.0);
    spElecGainStep->setDecimals(3);
    spElecGainStep->setSingleStep(0.001);
    spElecGainStep->setValue(m_tuneSettings.elecGainStep);

    //27. 拓展调谐offset步长
    QDoubleSpinBox* spElecOffsetStep = new QDoubleSpinBox();
    spElecOffsetStep->setRange(0.001, 1.0);
    spElecOffsetStep->setDecimals(3);
    spElecOffsetStep->setSingleStep(0.001);
    spElecOffsetStep->setValue(m_tuneSettings.elecOffsetStep);

    // 添加到布局
    elLayout->addLayout(createRow(basicTab, "电参R²目标值:", spElecR2Target, ""));
    elLayout->addLayout(createRow(basicTab, "电参R²最低阈值:", spElecR2Min, ""));
    elLayout->addLayout(createRow(basicTab, "电参 icon 步长:", spElecIconStep, ""));
    elLayout->addLayout(createRow(basicTab, "电参 mcp 步长:", spElecMcpStep, ""));
    elLayout->addLayout(createRow(basicTab, "电参最大调节次数:", spElecMaxCount, ""));
    elLayout->addLayout(createRow(basicTab, "小m/z阈值:", spElecSmallMz, ""));
    elLayout->addLayout(createRow(basicTab, "峰强调谐 Gain 步长:", spElecGainStep, ""));
    elLayout->addLayout(createRow(basicTab, "峰强调谐 Offset 步长:", spElecOffsetStep, ""));

    // 将两个 Tab 加入到面板
    tabWidget->addTab(basicTab, "🔧 基础参数");
    tabWidget->addTab(advTab, "🛠️ 高级参数");
    tabWidget->addTab(elTab, "🛠️ 电参数");
    mainLayout->addWidget(tabWidget);

    QHBoxLayout* bottomLayout = new QHBoxLayout();
    QPushButton* btnDlgLoad = new QPushButton("载入", &dialog);
    QPushButton* btnDlgSave = new QPushButton("保存", &dialog);
    QPushButton* btnDlgDefault = new QPushButton("设为默认值", &dialog);
    QPushButton* btnDlgDefaultGlobal = new QPushButton("使用全局默认值", &dialog);

    QFont btnFont; btnFont.setPointSize(13);
    btnDlgLoad->setFont(btnFont); btnDlgSave->setFont(btnFont);
    btnDlgDefault->setFont(btnFont); btnDlgDefaultGlobal->setFont(btnFont);
    btnDlgLoad->setCursor(Qt::PointingHandCursor); btnDlgSave->setCursor(Qt::PointingHandCursor);
    btnDlgDefault->setCursor(Qt::PointingHandCursor); btnDlgDefaultGlobal->setCursor(Qt::PointingHandCursor);

    connect(btnDlgDefaultGlobal, &QPushButton::clicked, &dialog, [=](){
        QString globalJsonPath = QCoreApplication::applicationDirPath() + "\\paramsAutoGlobal.json";
        auto saveCurrentToglobal = [&](){
            QVariantMap globalMap;
            //globalMap["maxIterations"] = m_tuneSettings.maxIterations;
            globalMap["rsdTarget"] = m_tuneSettings.rsdTarget;
            globalMap["peakWidthLower"] = m_tuneSettings.peakWidthLower;
            globalMap["peakWidthUpper"] = m_tuneSettings.peakWidthUpper;
            //globalMap["scanCountPerEval"] = m_tuneSettings.scanCountPerEval;
            //globalMap["peakWidthRatio"] = m_tuneSettings.peakWidthRatio;
            //globalMap["peakWidthRatio_1"] = m_tuneSettings.peakWidthRatio_1;
            //globalMap["gainStep"] = m_tuneSettings.gainStep;
            //globalMap["offsetStep"] = m_tuneSettings.offsetStep;
            //globalMap["minPeakHeight"] = m_tuneSettings.minPeakHeight;
            //globalMap["peakSearchWindow"] = m_tuneSettings.peakSearchWindow;
            //globalMap["stretchLower"] = m_tuneSettings.stretchLower;
            //globalMap["stretchUpper"] = m_tuneSettings.stretchUpper;
            //globalMap["distancePenaltyWeight"] = m_tuneSettings.distancePenaltyWeight;
            //globalMap["fineTunePosStep1"] = m_tuneSettings.fineTunePosStep1;
            //globalMap["fineTunePosStep2"] = m_tuneSettings.fineTunePosStep2;
            //globalMap["fineTuneVStep1"] = m_tuneSettings.fineTuneVStep1;
            //globalMap["fineTuneVStep2"] = m_tuneSettings.fineTuneVStep2;
            //globalMap["elecR2Target"] = m_tuneSettings.elecR2Target;
            //globalMap["elecR2Min"] = m_tuneSettings.elecR2Min;
            //globalMap["elecIconStep"] = m_tuneSettings.elecIconStep;
            //globalMap["elecMcpStep"] = m_tuneSettings.elecMcpStep;
            //globalMap["elecMaxCount"] = m_tuneSettings.elecMaxCount;
            //globalMap["elecSmallMzThreshold"] = m_tuneSettings.elecSmallMzThreshold;
            handleLogMessage("<span style='color: #0078D7;'>全局参数已被创建并保存至 paramsAutoGlobal.json</span>");
        };

        // 判断文件是否存在
        QFileInfo fileInfo(globalJsonPath);
        if(!fileInfo.exists()){
            saveCurrentToglobal();
        }
        else{
            //文件存在，尝试解析
            QVariantMap loadMap ;
            //文件为空
            if(loadMap.isEmpty()){
                saveCurrentToglobal();
            }
            //文件存在，读取覆盖
            else{
                //if(loadMap.contains("maxIterations")) spIter->setValue(loadMap["maxIterations"].toInt());
                if(loadMap.contains("rsdTarget")) spRsd->setValue(loadMap["rsdTarget"].toDouble());
                if(loadMap.contains("peakWidthLower")) spLower->setValue(loadMap["peakWidthLower"].toDouble());
                if(loadMap.contains("peakWidthUpper")) spUpper->setValue(loadMap["peakWidthUpper"].toDouble());
                //if(loadMap.contains("scanCountPerEval")) spScanCount->setValue(loadMap["scanCountPerEval"].toInt());
                //if(loadMap.contains("peakWidthRatio")) spRatio->setValue(loadMap["peakWidthRatio"].toDouble());
                //if(loadMap.contains("peakWidthRatio_1")) spRatio_1->setValue(loadMap["peakWidthRatio_1"].toDouble());
                //if(loadMap.contains("gainStep")) spGainStep->setValue(loadMap["gainStep"].toDouble());
                //if(loadMap.contains("offsetStep")) spOffsetStep->setValue(loadMap["offsetStep"].toDouble());
                //if(loadMap.contains("minPeakHeight")) spMinHeightExp->setValue(loadMap["minPeakHeight"].toDouble());
                //if(loadMap.contains("peakSearchWindow")) spWindow->setValue(loadMap["peakSearchWindow"].toDouble());
                //if(loadMap.contains("stretchLower")) spStretchLower->setValue(loadMap["stretchLower"].toDouble());
                //if(loadMap.contains("stretchUpper")) spStretchUpper->setValue(loadMap["stretchUpper"].toDouble());
                //if(loadMap.contains("distancePenaltyWeight")) spDistPenalty->setValue(loadMap["distancePenaltyWeight"].toDouble());
                //if(loadMap.contains("fineTunePosStep1")) spFinePosStep_1->setValue(loadMap["fineTunePosStep1"].toDouble());
                //if(loadMap.contains("fineTunePosStep2")) spFinePosStep_2->setValue(loadMap["fineTunePosStep2"].toDouble());
                //if(loadMap.contains("fineTuneVStep1")) spFineVStep_1->setValue(loadMap["fineTuneVStep1"].toDouble());
                //if(loadMap.contains("fineTuneVStep2")) spFineVStep_2->setValue(loadMap["fineTuneVStep2"].toDouble());
                //if(loadMap.contains("elecR2Target")) spElecR2Target->setValue(loadMap["elecR2Target"].toDouble());
                //if(loadMap.contains("elecR2Min")) spElecR2Min->setValue(loadMap["elecR2Min"].toDouble());
                //if(loadMap.contains("elecIconStep")) spElecIconStep->setValue(loadMap["elecIconStep"].toDouble());
                //if(loadMap.contains("elecMcpStep")) spElecMcpStep->setValue(loadMap["elecMcpStep"].toDouble());
                //if(loadMap.contains("elecMaxCount")) spElecMaxCount->setValue(loadMap["elecMaxCount"].toInt());
                //if(loadMap.contains("elecSmallMzThreshold")) spElecSmallMz->setValue(loadMap["elecSmallMzThreshold"].toDouble());

                handleLogMessage("<span style='color: #0078D7;'>已读取全局默认值，请确认无误后点击应用！</span>");
            }
        }
    });

    connect(btnDlgSave, &QPushButton::clicked, this, &AutoTuningWidget::onSaveTableClicked);
    connect(btnDlgLoad, &QPushButton::clicked, this, &AutoTuningWidget::onLoadParamsClicked);
    connect(btnDlgLoad, &QPushButton::clicked, this, [=](){
        spIter->setValue(m_tuneSettings.maxIterations);
        spRsd->setValue(m_tuneSettings.rsdTarget);
        spLower->setValue(m_tuneSettings.peakWidthLower);
        spUpper->setValue(m_tuneSettings.peakWidthUpper);
        spScanCount->setValue(m_tuneSettings.scanCountPerEval);
        spMaxLength->setValue(m_tuneSettings.averageMaxLength);
        spRatio->setValue(m_tuneSettings.peakWidthRatio * 100);
        spRatio_1->setValue(m_tuneSettings.peakWidthRatio_1 * 100);
        chkCrv->setChecked(m_tuneSettings.positionCrvEnabled);
        spCrv_R2->setValue(m_tuneSettings.positionCrv_R2 * 100);
        spGainStep->setValue(m_tuneSettings.gainStep);
        spOffsetStep->setValue(m_tuneSettings.offsetStep);
        spMinHeightExp->setValue(std::log10(m_tuneSettings.minPeakHeight));
        spWindow->setValue(m_tuneSettings.peakSearchWindow);
        spStretchLower->setValue(m_tuneSettings.stretchLower);
        spStretchUpper->setValue(m_tuneSettings.stretchUpper);
        spDistPenalty->setValue(m_tuneSettings.distancePenaltyWeight);
        spFinePosStep_1->setValue(m_tuneSettings.fineTunePosStep1);
        spFinePosStep_2->setValue(m_tuneSettings.fineTunePosStep2);
        spFineVStep_1->setValue(m_tuneSettings.fineTuneVStep1);
        spFineVStep_2->setValue(m_tuneSettings.fineTuneVStep2);
        spElecR2Target->setValue(m_tuneSettings.elecR2Target);
        spElecR2Min->setValue(m_tuneSettings.elecR2Min);
        spElecIconStep->setValue(m_tuneSettings.elecIconStep);
        spElecMcpStep->setValue(m_tuneSettings.elecMcpStep);
        spElecMaxCount->setValue(m_tuneSettings.elecMaxCount);
        spElecSmallMz->setValue(m_tuneSettings.elecSmallMzThreshold);
        spElecGainStep->setValue(m_tuneSettings.elecGainStep);
        spElecOffsetStep->setValue(m_tuneSettings.elecOffsetStep);

        handleLogMessage("<span style='color: #D13438;'>参数载入成功，请点击应用以生效！</span>");
    });

    connect(btnDlgDefault, &QPushButton::clicked, &dialog, [=](){
        if(m_configFilePath.isEmpty())
        {
            return;
        }

        // 先执行应用
        {
            //保存基础参数
            m_tuneSettings.maxIterations = spIter->value();
            m_tuneSettings.rsdTarget = spRsd->value();
            m_tuneSettings.peakWidthLower = spLower->value();
            m_tuneSettings.peakWidthUpper = spUpper->value();
            m_tuneSettings.scanCountPerEval = spScanCount->value();
            m_tuneSettings.averageMaxLength = spMaxLength->value();
            m_tuneSettings.peakWidthRatio = spRatio->value() / 100.0;
            m_tuneSettings.peakWidthRatio_1 = spRatio_1->value() / 100.0;
            m_tuneSettings.positionCrvEnabled = chkCrv->isChecked();
            m_tuneSettings.positionCrv_R2 = spCrv_R2->value() / 100.0;

            //高级参数
            m_tuneSettings.gainStep = spGainStep->value();
            m_tuneSettings.offsetStep = spOffsetStep->value();
            m_tuneSettings.minPeakHeight = std::pow(10, spMinHeightExp->value());
            m_tuneSettings.peakSearchWindow = spWindow->value();
            m_tuneSettings.stretchLower = spStretchLower->value();
            m_tuneSettings.stretchUpper = spStretchUpper->value();
            m_tuneSettings.distancePenaltyWeight = spDistPenalty->value();
            m_tuneSettings.fineTunePosStep1 = spFinePosStep_1->value();
            m_tuneSettings.fineTunePosStep2 = spFinePosStep_2->value();
            m_tuneSettings.fineTuneVStep1 = spFineVStep_1->value();
            m_tuneSettings.fineTuneVStep2 = spFineVStep_2->value();

            //电参数
            m_tuneSettings.elecR2Target = spElecR2Target->value();
            m_tuneSettings.elecR2Min = spElecR2Min->value();
            m_tuneSettings.elecIconStep = spElecIconStep->value();
            m_tuneSettings.elecMcpStep = spElecMcpStep->value();
            m_tuneSettings.elecMaxCount = spElecMaxCount->value();
            m_tuneSettings.elecSmallMzThreshold = spElecSmallMz->value();
            m_tuneSettings.elecGainStep = spElecGainStep->value();
            m_tuneSettings.elecOffsetStep = spElecOffsetStep->value();
        }

        QString jsonPath = m_configFilePath + "/params.json";

        QVariantMap defaultMap;
        defaultMap["maxIterations"] = m_tuneSettings.maxIterations;
        defaultMap["rsdTarget"] = m_tuneSettings.rsdTarget;
        defaultMap["peakWidthLower"] = m_tuneSettings.peakWidthLower;
        defaultMap["peakWidthUpper"] = m_tuneSettings.peakWidthUpper;
        defaultMap["scanCountPerEval"] = m_tuneSettings.scanCountPerEval;
        defaultMap["averageMaxLength"] = m_tuneSettings.averageMaxLength;
        defaultMap["peakWidthRatio"] = m_tuneSettings.peakWidthRatio;
        defaultMap["peakWidthRatio_1"] = m_tuneSettings.peakWidthRatio_1;
        defaultMap["positionCrvEnabled"] = m_tuneSettings.positionCrvEnabled;
        defaultMap["positionCrv_R2"] = m_tuneSettings.positionCrv_R2;
        defaultMap["gainStep"] = m_tuneSettings.gainStep;
        defaultMap["offsetStep"] = m_tuneSettings.offsetStep;
        defaultMap["minPeakHeight"] = m_tuneSettings.minPeakHeight;
        defaultMap["peakSearchWindow"] = m_tuneSettings.peakSearchWindow;
        defaultMap["stretchLower"] = m_tuneSettings.stretchLower;
        defaultMap["stretchUpper"] = m_tuneSettings.stretchUpper;
        defaultMap["distancePenaltyWeight"] = m_tuneSettings.distancePenaltyWeight;
        defaultMap["fineTunePosStep1"] = m_tuneSettings.fineTunePosStep1;
        defaultMap["fineTunePosStep2"] = m_tuneSettings.fineTunePosStep2;
        defaultMap["fineTuneVStep1"] = m_tuneSettings.fineTuneVStep1;
        defaultMap["fineTuneVStep2"] = m_tuneSettings.fineTuneVStep2;
        defaultMap["elecR2Target"] = m_tuneSettings.elecR2Target;
        defaultMap["elecR2Min"] = m_tuneSettings.elecR2Min;
        defaultMap["elecIconStep"] = m_tuneSettings.elecIconStep;
        defaultMap["elecMcpStep"] = m_tuneSettings.elecMcpStep;
        defaultMap["elecMaxCount"] = m_tuneSettings.elecMaxCount;
        defaultMap["elecSmallMzThreshold"] = m_tuneSettings.elecSmallMzThreshold;
        defaultMap["elecGainStep"] = m_tuneSettings.elecGainStep;
        defaultMap["elecOffsetStep"] = m_tuneSettings.elecOffsetStep;

        handleLogMessage("<span style='color: #0078D7;'>默认值设置成功，默认配置文件params.json已保存</span>");
    });

    // 确认与取消按钮
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->setCursor(Qt::PointingHandCursor);
    buttonBox->setStyleSheet(R"(QPushButton { background-color: #dbd6d6;
                             color: #161515;
                             border: none;
                             border-radius: 4px;
                             padding: 6px 24px;
                             font-size: 15px;
                             font-weight: bold;}
                             QPushButton:hover { background-color: #c1bbbb; }
                             QPushButton:pressed { background-color: #a8a1a1; padding-top: 2px; }
                             )");
    buttonBox->button(QDialogButtonBox::Ok)->setText("应用");
    buttonBox->button(QDialogButtonBox::Cancel)->setText("取消");
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, [=](){
        //保存基础参数
        m_tuneSettings.maxIterations = spIter->value();
        m_tuneSettings.rsdTarget = spRsd->value();
        m_tuneSettings.peakWidthLower = spLower->value();
        m_tuneSettings.peakWidthUpper = spUpper->value();
        m_tuneSettings.scanCountPerEval = spScanCount->value();
        m_tuneSettings.averageMaxLength = spMaxLength->value();
        m_tuneSettings.peakWidthRatio = spRatio->value() / 100.0;
        m_tuneSettings.peakWidthRatio_1 = spRatio_1->value() / 100.0;
        m_tuneSettings.positionCrvEnabled = chkCrv->isChecked();
        m_tuneSettings.positionCrv_R2 = spCrv_R2->value() / 100.0;

        //高级参数
        m_tuneSettings.gainStep = spGainStep->value();
        m_tuneSettings.offsetStep = spOffsetStep->value();
        m_tuneSettings.minPeakHeight = std::pow(10, spMinHeightExp->value());
        m_tuneSettings.peakSearchWindow = spWindow->value();
        m_tuneSettings.stretchLower = spStretchLower->value();
        m_tuneSettings.stretchUpper = spStretchUpper->value();
        m_tuneSettings.distancePenaltyWeight = spDistPenalty->value();
        m_tuneSettings.fineTunePosStep1 = spFinePosStep_1->value();
        m_tuneSettings.fineTunePosStep2 = spFinePosStep_2->value();
        m_tuneSettings.fineTuneVStep1 = spFineVStep_1->value();
        m_tuneSettings.fineTuneVStep2 = spFineVStep_2->value();

        //电参数
        m_tuneSettings.elecR2Target = spElecR2Target->value();
        m_tuneSettings.elecR2Min = spElecR2Min->value();
        m_tuneSettings.elecIconStep = spElecIconStep->value();
        m_tuneSettings.elecMcpStep = spElecMcpStep->value();
        m_tuneSettings.elecMaxCount = spElecMaxCount->value();
        m_tuneSettings.elecSmallMzThreshold = spElecSmallMz->value();
        m_tuneSettings.elecGainStep = spElecGainStep->value();
        m_tuneSettings.elecOffsetStep = spElecOffsetStep->value();

        handleLogMessage(QString("<span style='color: #0078D7;'>⚙️ 已更新自动调谐参数。</span>"));
    });
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    bottomLayout->addWidget(btnDlgLoad);
    bottomLayout->addWidget(btnDlgSave);
    bottomLayout->addWidget(btnDlgDefault);
    bottomLayout->addWidget(btnDlgDefaultGlobal);
    bottomLayout->addStretch();
    bottomLayout->addWidget(buttonBox);
    mainLayout->addLayout(bottomLayout);

    dialog.exec();

}

//保存参数表格文件
void AutoTuningWidget::onSaveTableClicked()
{
    if(m_configFilePath=="" || QDir(m_configFilePath).exists()==false) //如果配置文件夹路径为空或者不存在此路径
    {
        QMessageBox::critical(this,"保存失败","配置文件夹路径为空或不存在！");
        return;
    }
    QString defaultPath = m_configFilePath;

    QDir dir;
    if (!dir.exists(defaultPath)) {
        dir.mkpath(defaultPath); // 如果文件夹不存在，自动创建！
    }

    // 弹出的对话框会直接定位到path文件夹
    QString fileName = QFileDialog::getSaveFileName(this, "保存参数", defaultPath, "json 文件 (*.json);;所有文件 (*.*)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        handleLogMessage(QString("<span style='color: #D13438;'>保存失败：无法写入文件 %1</span>").arg(fileName));
        return;
    }

    QVariantMap saveMap;
    // 表格参数保存
    QVariantList tableLst;
    int row = m_paramTable->rowCount();
    for(int r = 0; r < row ; ++r)
    {
        QVariantMap rowMap;
        QTableWidgetItem* item = m_paramTable->item(r,0);
        if(!item || item->text().trimmed().isEmpty())
        {
            continue;
        }
        QString mzStr = item->text();
        if(mzStr == "")
        {
            continue;
        }
        rowMap["mz"] = mzStr;

        item = m_paramTable->item(r, 1);
        rowMap["position"] = item->text();

        item = m_paramTable->item(r, 2);
        rowMap["gain"] = item->text();

        item = m_paramTable->item(r, 3);
        rowMap["offset"] = item->text();

        item = m_paramTable->item(r, 4);
        rowMap["v"] = item->text();

        item = m_paramTable->item(r, 5);
        rowMap["reso"] = item->text();

        item = m_paramTable->item(r, 6);
        rowMap["dac"] = item->text();

        item = m_paramTable->item(r, 7);
        rowMap["code"] = item->text();
        tableLst.append(rowMap);
    }
    saveMap["paramsTable"] = tableLst;

    // 基础参数保存
    QVariantList baseLst;
    QVariantMap baseMap;
    baseMap["maxIterations"] = m_tuneSettings.maxIterations;
    baseMap["rsdTarget"] = m_tuneSettings.rsdTarget;
    baseMap["peakWidthLower"] = m_tuneSettings.peakWidthLower;
    baseMap["peakWidthUpper"] = m_tuneSettings.peakWidthUpper;
    baseMap["scanCountPerEval"] = m_tuneSettings.scanCountPerEval;
    baseMap["averageMaxLength"] = m_tuneSettings.averageMaxLength;
    baseMap["peakWidthRatio"] = m_tuneSettings.peakWidthRatio;
    baseMap["peakWidthRatio_1"] = m_tuneSettings.peakWidthRatio_1;
    baseMap["positionCrvEnabled"] = m_tuneSettings.positionCrvEnabled;
    baseMap["positionCrv_R2"] = m_tuneSettings.positionCrv_R2;
    baseLst.append(baseMap);
    saveMap["baseParams"] = baseLst;

    // 高级参数保存
    QVariantList advanceLst;
    QVariantMap advanceMap;
    advanceMap["gainStep"] = m_tuneSettings.gainStep;
    advanceMap["offsetStep"] = m_tuneSettings.offsetStep;
    advanceMap["minPeakHeight"] = m_tuneSettings.minPeakHeight;
    advanceMap["peakSearchWindow"] = m_tuneSettings.peakSearchWindow;
    advanceMap["stretchLower"] = m_tuneSettings.stretchLower;
    advanceMap["stretchUpper"] = m_tuneSettings.stretchUpper;
    advanceMap["distancePenaltyWeight"] = m_tuneSettings.distancePenaltyWeight;
    advanceMap["fineTunePosStep1"] = m_tuneSettings.fineTunePosStep1;
    advanceMap["fineTunePosStep2"] = m_tuneSettings.fineTunePosStep2;
    advanceMap["fineTuneVStep1"] = m_tuneSettings.fineTuneVStep1;
    advanceMap["fineTuneVStep2"] = m_tuneSettings.fineTuneVStep2;
    advanceLst.append(advanceMap);
    saveMap["advanceParams"] = advanceLst;

    // 电参数保存
    QVariantList elecLst;
    QVariantMap elecMap;
    elecMap["elecR2Target"] = m_tuneSettings.elecR2Target;
    elecMap["elecR2Min"] = m_tuneSettings.elecR2Min;
    elecMap["elecIconStep"] = m_tuneSettings.elecIconStep;
    elecMap["elecMcpStep"] = m_tuneSettings.elecMcpStep;
    elecMap["elecMaxCount"] = m_tuneSettings.elecMaxCount;
    elecMap["elecSmallMzThreshold"] = m_tuneSettings.elecSmallMzThreshold;
    elecMap["elecGainStep"] = m_tuneSettings.elecGainStep;
    elecMap["elecOffsetStep"] = m_tuneSettings.elecOffsetStep;
    elecLst.append(elecMap);
    saveMap["elecParams"] = elecLst;
    file.close();
    handleLogMessage(QString("<span style='color: #107C10;'>💾 成功保存参数至: %1</span>").arg(fileName));
}

//载入参数表格文件
void AutoTuningWidget::onLoadTableClicked()
{
    if(m_configFilePath=="" || QDir(m_configFilePath).exists()==false) //如果配置文件夹路径为空或者不存在此路径
    {
        QMessageBox::critical(this,"加载失败","配置文件夹路径为空或不存在！");
        return;
    }
    QString defaultPath = m_configFilePath;

    QDir dir;
    if (!dir.exists(defaultPath)) dir.mkpath(defaultPath);

    // 默认定位到文件夹
    QString fileName = QFileDialog::getOpenFileName(this, "载入参数表格", defaultPath, "json 文件 (*.json);;所有文件 (*.*)");
    if (fileName.isEmpty()) return;

    // 将 JSON 解析为 QVariantMap
    QVariantMap loadMap;
    if (loadMap.isEmpty()) {
        handleLogMessage(QString("<span style='color: #D13438;'>载入失败：文件解析为空或格式错误 %1</span>").arg(fileName));
        return;
    }

    // 清理现有数据与信号拦截，防止频繁刷新 UI 卡死
    m_paramTable->setRowCount(0);
    m_paramTable->blockSignals(true);
    m_cmbFineTuneMz->blockSignals(true);
    m_cmbFineTuneMz->clear();

    // 读取表格参数 (Table Params)
    QVariantList tableLst = loadMap["paramsTable"].toList();
    for (int row = 0; row < tableLst.count(); ++row) {
        QVariantMap rowMap = tableLst[row].toMap();

        m_paramTable->insertRow(row);

        // 提取 m/z 并配置颜色
        QString mzStr = rowMap["mz"].toString();
        QTableWidgetItem* itemMz = new QTableWidgetItem(mzStr);
        itemMz->setForeground(QBrush(QColor(0, 150, 255)));
        itemMz->setData(Qt::UserRole, mzStr);
        m_paramTable->setItem(row, 0, itemMz);

        // 填入黑色常规数据
        m_paramTable->setItem(row, 1, new QTableWidgetItem(rowMap["position"].toString()));
        m_paramTable->setItem(row, 2, new QTableWidgetItem(rowMap["gain"].toString()));
        m_paramTable->setItem(row, 3, new QTableWidgetItem(rowMap["offset"].toString()));
        m_paramTable->setItem(row, 4, new QTableWidgetItem(rowMap["v"].toString()));

        // 填入红色关注数据
        QTableWidgetItem* itemReso = new QTableWidgetItem(rowMap["reso"].toString());
        itemReso->setForeground(QBrush(QColor(220, 50, 50)));
        m_paramTable->setItem(row, 5, itemReso);

        QTableWidgetItem* itemDac = new QTableWidgetItem(rowMap["dac"].toString());
        itemDac->setForeground(QBrush(QColor(220, 50, 50)));
        m_paramTable->setItem(row, 6, itemDac);

        QTableWidgetItem* itemCode = new QTableWidgetItem(rowMap["code"].toString());
        itemCode->setForeground(QBrush(QColor(220, 50, 50)));
        m_paramTable->setItem(row, 7, itemCode);

        // 生效列：加载时默认不勾选
        m_paramTable->setItem(row, 8, createCheckItem());
    }

    // UI 格式收尾恢复
    // 载入参数表格后追加 1 行空白
    int currentRows = m_paramTable->rowCount();
    m_paramTable->setRowCount(currentRows + 1);
    for(int i = currentRows; i < m_paramTable->rowCount(); ++i) {
        for(int c = 0; c < 9; ++c) {
            m_paramTable->setItem(i, c, new QTableWidgetItem(""));
        }
        m_paramTable->setItem(i, 8, createCheckItem());
    }

    // 重新合并 Gain 和 Offset 列
    m_paramTable->setSpan(0, 2, m_paramTable->rowCount(), 1);
    m_paramTable->setSpan(0, 3, m_paramTable->rowCount(), 1);
    if(m_paramTable->item(0, 2)) m_paramTable->item(0, 2)->setTextAlignment(Qt::AlignCenter);
    if(m_paramTable->item(0, 3)) m_paramTable->item(0, 3)->setTextAlignment(Qt::AlignCenter);

    m_paramTable->blockSignals(false);
    refreshFineTuneMzCombo();

    handleLogMessage(QString("<span style='color: #107C10;'>📂 成功载入参数表格: %1</span>").arg(fileName));
}

void AutoTuningWidget::onDeleteEleRow()
{
    int row = m_elTable->currentRow();
    if (row < 0 || m_elTable->rowCount() <= 1) return; // 至少保留1行

    // 删除行（先删数据，再删行，避免悬垂指针）
    for (int col = 0; col < m_elTable->columnCount(); ++col) {
        delete m_elTable->takeItem(row, col);
    }
    m_elTable->removeRow(row);
}

void AutoTuningWidget::onInsertEleRow()
{
    int row = m_elTable->currentRow();
    if (row < 0) row = m_elTable->rowCount() - 1; // 没选中则插到最后
    if (m_elTable->rowCount() > 15){
        QMessageBox::warning(this, "错误", "有效数据行数大于16行！");
        return;
    }

    // 2. 插入新行 + 预填充所有列（你的方案1）
    int newRow = row + 1;
    m_elTable->insertRow(newRow);
    for (int col = 0; col < m_elTable->columnCount(); ++col) {
        m_elTable->setItem(newRow, col, new QTableWidgetItem(""));
    }
    m_elTable->setItem(newRow, m_elTable->columnCount() - 1, createCheckItem());

}

void AutoTuningWidget::onEleTableContextMenu(const QPoint &pos)
{
    // pos 是相对于 QTableWidget 的局部坐标
    // 获取当前点击位置的单元格
    QTableWidgetItem *item = m_elTable->itemAt(pos);

    // 只在有效单元格上显示菜单（空白处不显示）
    if (!item) {
        return;
    }

    // 将局部坐标转为全局坐标，在鼠标位置弹出菜单
    m_contextEleMenu->exec(m_elTable->mapToGlobal(pos));
}

void AutoTuningWidget::onLoadParamsClicked()
{
    if(m_configFilePath=="" || QDir(m_configFilePath).exists()==false) //如果配置文件夹路径为空或者不存在此路径
    {
        QMessageBox::critical(this,"加载失败","配置文件夹路径为空或不存在！");
        return;
    }
    QString defaultPath = m_configFilePath;

    QDir dir;
    if (!dir.exists(defaultPath)) dir.mkpath(defaultPath);

    // 默认定位到文件夹
    QString fileName = QFileDialog::getOpenFileName(this, "载入参数表格", defaultPath, "json 文件 (*.json);;所有文件 (*.*)");
    if (fileName.isEmpty()) return;

    // 将 JSON 解析为 QVariantMap
    QVariantMap loadMap ;
    if (loadMap.isEmpty()) {
        handleLogMessage(QString("<span style='color: #D13438;'>载入失败：文件解析为空或格式错误 %1</span>").arg(fileName));
        return;
    }

    // 1. 读取基础参数 (Base Params)
    QVariantList baseLst = loadMap["baseParams"].toList();
    if (!baseLst.isEmpty()) {
        QVariantMap baseMap = baseLst.first().toMap();
        m_tuneSettings.maxIterations = baseMap["maxIterations"].toInt();
        m_tuneSettings.rsdTarget = baseMap["rsdTarget"].toDouble();
        m_tuneSettings.peakWidthLower = baseMap["peakWidthLower"].toDouble();
        m_tuneSettings.peakWidthUpper = baseMap["peakWidthUpper"].toDouble();
        m_tuneSettings.scanCountPerEval = baseMap["scanCountPerEval"].toInt();
        m_tuneSettings.averageMaxLength = baseMap["averageMaxLength"].toInt();
        m_tuneSettings.peakWidthRatio = baseMap["peakWidthRatio"].toDouble();
        m_tuneSettings.peakWidthRatio_1 = baseMap["peakWidthRatio_1"].toDouble();
        m_tuneSettings.positionCrvEnabled = baseMap["positionCrvEnabled"].toBool();
        m_tuneSettings.positionCrv_R2 = baseMap["positionCrv_R2"].toDouble();
    }

    // 2. 读取高级参数 (Advance Params)
    QVariantList advanceLst = loadMap["advanceParams"].toList();
    if (!advanceLst.isEmpty()) {
        QVariantMap advanceMap = advanceLst.first().toMap();
        m_tuneSettings.gainStep = advanceMap["gainStep"].toDouble();
        m_tuneSettings.offsetStep = advanceMap["offsetStep"].toDouble();
        m_tuneSettings.minPeakHeight = advanceMap["minPeakHeight"].toDouble();
        m_tuneSettings.peakSearchWindow = advanceMap["peakSearchWindow"].toDouble();
        m_tuneSettings.stretchLower = advanceMap["stretchLower"].toDouble();
        m_tuneSettings.stretchUpper = advanceMap["stretchUpper"].toDouble();
        m_tuneSettings.distancePenaltyWeight = advanceMap["distancePenaltyWeight"].toDouble();
        m_tuneSettings.fineTunePosStep1 = advanceMap["fineTunePosStep1"].toDouble();
        m_tuneSettings.fineTunePosStep2 = advanceMap["fineTunePosStep2"].toDouble();
        m_tuneSettings.fineTuneVStep1 = advanceMap["fineTuneVStep1"].toDouble();
        m_tuneSettings.fineTuneVStep2 = advanceMap["fineTuneVStep2"].toDouble();
    }

    // 3.读取电参数（Elec Params）
    QVariantList elecLst = loadMap["elecParams"].toList();
    if (!elecLst.isEmpty()) {
        QVariantMap elecMap = elecLst.first().toMap();
        m_tuneSettings.elecR2Target = elecMap["elecR2Target"].toDouble();
        m_tuneSettings.elecR2Min = elecMap["elecR2Min"].toDouble();
        m_tuneSettings.elecIconStep = elecMap["elecIconStep"].toInt();
        m_tuneSettings.elecMcpStep = elecMap["elecMcpStep"].toInt();
        m_tuneSettings.elecMaxCount = elecMap["elecMaxCount"].toInt();
        m_tuneSettings.elecSmallMzThreshold = elecMap["elecSmallMzThreshold"].toDouble();
        m_tuneSettings.elecGainStep = elecMap["elecGainStep"].toDouble();
        m_tuneSettings.elecOffsetStep = elecMap["elecOffsetStep"].toDouble();
    }

    handleLogMessage(QString("<span style='color: #107C10;'>📂 成功载入配置表: %1</span>").arg(fileName));
}

// Adv 拓展表格：保存参数
// =========================================================
void AutoTuningWidget::onSaveAdvTableClicked()
{
    if(m_configFilePath == "" || QDir(m_configFilePath).exists() == false) {
        QMessageBox::critical(this, "保存失败", "配置文件夹路径为空或不存在！");
        return;
    }
    QString defaultPath = m_configFilePath;
    QDir dir;
    if (!dir.exists(defaultPath)) {
        dir.mkpath(defaultPath);
    }

    QString fileName = QFileDialog::getSaveFileName(this, "保存 Adv 参数表格", defaultPath, "json 文件 (*.json);;所有文件 (*.*)");
    if (fileName.isEmpty()) return;

    QVariantMap saveMap;
    QVariantList tableLst;
    int row = m_elTable->rowCount();
    for(int r = 0; r < row ; ++r)
    {
        QTableWidgetItem* item = m_elTable->item(r, 0);
        if(!item || item->text().trimmed().isEmpty()) continue;
        QString mzStr = item->text();
        if(mzStr == "") continue;

        QVariantMap rowMap;
        rowMap["mz"] = mzStr;
        rowMap["position"] = m_elTable->item(r, 1)->text();
        rowMap["gain"]     = m_elTable->item(r, 2)->text();
        rowMap["offset"]   = m_elTable->item(r, 3)->text();
        rowMap["v"]        = m_elTable->item(r, 4)->text();
        rowMap["reso"]     = m_elTable->item(r, 5)->text();
        rowMap["dac"]      = m_elTable->item(r, 6)->text();
        rowMap["code"]     = m_elTable->item(r, 7)->text();
        rowMap["DestInten"]= m_elTable->item(r, 8)->text(); // 保存目标强度

        tableLst.append(rowMap);
    }

    // 使用专属键值存储
    saveMap["advParamsTable"] = tableLst;

    handleLogMessage(QString("<span style='color: #107C10;'>💾 成功保存 Adv 参数表格至: %1</span>").arg(fileName));
}

// =========================================================
// Adv 拓展表格：载入参数
// =========================================================
void AutoTuningWidget::onLoadAdvTableClicked()
{
    if(m_configFilePath == "" || QDir(m_configFilePath).exists() == false) {
        QMessageBox::critical(this, "加载失败", "配置文件夹路径为空或不存在！");
        return;
    }
    QString defaultPath = m_configFilePath;
    QDir dir;
    if (!dir.exists(defaultPath)) dir.mkpath(defaultPath);

    QString fileName = QFileDialog::getOpenFileName(this, "载入 Adv 参数表格", defaultPath, "json 文件 (*.json);;所有文件 (*.*)");
    if (fileName.isEmpty()) return;

    QVariantMap loadMap ;
    if (loadMap.isEmpty()) {
        handleLogMessage(QString("<span style='color: #D13438;'>载入失败：文件解析为空或格式错误 %1</span>").arg(fileName));
        return;
    }

    // 尝试提取 advParamsTable，如果用户选了普通的 auto 表格，兼容提取 paramsTable
    QVariantList tableLst = loadMap["advParamsTable"].toList();
    if(tableLst.isEmpty()) {
        tableLst = loadMap["paramsTable"].toList();
    }

    if (tableLst.isEmpty()) {
        handleLogMessage("<span style='color: #D13438;'>载入失败：未在文件中找到有效的表格数据！</span>");
        return;
    }

    // 清空现有表格
    m_elTable->setRowCount(0);

    for (int row = 0; row < tableLst.count(); ++row) {
        QVariantMap rowMap = tableLst[row].toMap();
        m_elTable->insertRow(row);

        // m/z (蓝色)
        QTableWidgetItem* itemMz = new QTableWidgetItem(rowMap["mz"].toString());
        itemMz->setForeground(QBrush(QColor(0, 150, 255)));
        m_elTable->setItem(row, 0, itemMz);

        m_elTable->setItem(row, 1, new QTableWidgetItem(rowMap["position"].toString()));

        // Gain 和 Offset 居中对齐
        QTableWidgetItem* itemGain = new QTableWidgetItem(rowMap["gain"].toString());
        itemGain->setTextAlignment(Qt::AlignCenter);
        m_elTable->setItem(row, 2, itemGain);

        QTableWidgetItem* itemOffset = new QTableWidgetItem(rowMap["offset"].toString());
        itemOffset->setTextAlignment(Qt::AlignCenter);
        m_elTable->setItem(row, 3, itemOffset);

        m_elTable->setItem(row, 4, new QTableWidgetItem(rowMap["v"].toString()));

        // Reso, DAC, Code (红色)
        QTableWidgetItem* itemReso = new QTableWidgetItem(rowMap["reso"].toString());
        itemReso->setForeground(QBrush(QColor(220, 50, 50)));
        m_elTable->setItem(row, 5, itemReso);

        QTableWidgetItem* itemDac = new QTableWidgetItem(rowMap["dac"].toString());
        itemDac->setForeground(QBrush(QColor(220, 50, 50)));
        m_elTable->setItem(row, 6, itemDac);

        QTableWidgetItem* itemCode = new QTableWidgetItem(rowMap["code"].toString());
        itemCode->setForeground(QBrush(QColor(220, 50, 50)));
        m_elTable->setItem(row, 7, itemCode);

        // DestInten (绿色，具备后备机制防止加载老表时报错)
        QString intenVal = rowMap.contains("DestInten") ? rowMap["DestInten"].toString() : "1.0e-11";
        QTableWidgetItem* itemInten = new QTableWidgetItem(intenVal);
        itemInten->setForeground(QBrush(QColor(0, 150, 0)));
        m_elTable->setItem(row, 8, itemInten);

        // 生效列：加载时默认不勾选
        m_elTable->setItem(row, 9, createCheckItem());
    }

    // 追加 3 行空白行备用
    int currentRows = m_elTable->rowCount();
    m_elTable->setRowCount(currentRows + 1);
    for(int i = currentRows; i < m_elTable->rowCount(); ++i) {
        for(int c = 0; c < 10; ++c) {
            m_elTable->setItem(i, c, new QTableWidgetItem(""));
        }
        m_elTable->setItem(i, 9, createCheckItem());
    }

    handleLogMessage(QString("<span style='color: #107C10;'>📂 成功载入 Adv 参数表格: %1</span>").arg(fileName));
}
//保存日志文件
void AutoTuningWidget::onSaveLogClicked()
{
    if(m_configFilePath=="" || QDir(m_configFilePath).exists()==false) //如果配置文件夹路径为空或者不存在此路径
    {
        QMessageBox::critical(this,"保存失败","配置文件夹路径为空或不存在！");
        return;
    }
    QString defaultPath = m_configFilePath + "/LOG";

    QDir dir;
    if (!dir.exists(defaultPath)) {
        dir.mkpath(defaultPath); // 自动创建日志文件夹
    }

    // 自动生成带时间戳的默认文件名 (例如: AutoTuneLog_20260509_1530.txt)
    QString timeStamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmm");
    QString defaultFileName = defaultPath + "/AutoTuneLog_" + timeStamp + ".txt";

    // 弹出保存文件对话框，并默认填好路径和时间戳文件名
    QString fileName = QFileDialog::getSaveFileName(this, "保存运行日志", defaultFileName, "文本文件 (*.txt);;所有文件 (*.*)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        handleLogMessage(QString("<span style='color: #D13438;'>保存日志失败：无法写入文件 %1</span>").arg(fileName));
        return;
    }

    QTextStream out(&file);
    // 将日志框中的所有纯文本内容（不含HTML颜色标签）写入文件
    out << m_logTextEdit->toPlainText();

    file.close();

    // 成功后，在日志框内部再追加一条提示
    handleLogMessage(QString("<span style='color: #107C10;'>💾 运行日志已成功保存至: %1</span>").arg(fileName));
}

// ==== 精调控制槽函数 ====
void AutoTuningWidget::onFineTuneTargetChanged(int index) {
    if (index < 0 || index >= m_paramTable->rowCount()) return;
    m_hiddenPosition = m_paramTable->item(index, 1)->text().toDouble();
    m_hiddenV = m_paramTable->item(index, 4)->text().toDouble();

    // 更新基准原点，并归零拉杆
    m_initialPosition = m_hiddenPosition;
    m_initialV = m_hiddenV;
    m_sliderPos->setValue(0);
    m_sliderV->setValue(0);

    // 从 UI 缓存中提取数据并刷新显示
    refreshLocalPlot();
}

void AutoTuningWidget::onFineTuneAdjustClicked() {
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if(!btn) return;

    int row = m_cmbFineTuneMz->currentData().toInt();

    // 空指针与越界拦截
    // 防止软件刚启动表格未加载完成时，强行读取导致段错误崩溃
    if (row < 0 || !m_paramTable->item(row, 2) || !m_paramTable->item(row, 3)) {
        handleLogMessage("错误：参数表格尚未初始化完毕，请稍后再试！");
        return;
    }

    QString target = btn->property("t").toString();
    double stepType = btn->property("s").toInt();
    double step = 0.0;

    if (target == "pos")
    {
        step = (std::abs(stepType) == 1) ? m_tuneSettings.fineTunePosStep1 :m_tuneSettings.fineTunePosStep2;
        if(stepType < 0) step = -step;
        // 1.计算期望的最新位置
        double desiredPos = m_hiddenPosition - step;
        // 2.限定调整范围
        double minBound = m_initialPosition - m_tuneSettings.fineTunePosStep2 * 10;
        double maxBound = m_initialPosition + m_tuneSettings.fineTunePosStep2 * 10;
        m_hiddenPosition = std::max(minBound,std::min(desiredPos, maxBound));
        // 3.将真实偏差映射给拉杆显示
        int posOffset = static_cast<int>(m_initialPosition - m_hiddenPosition);
        m_sliderPos->setValue(posOffset);
        // 如果到底了给出警告
        if(desiredPos < minBound || desiredPos > maxBound)
        {
            handleLogMessage("<span style='color: #D13438;'>提示：已达到Position单次最大调节上限!</span>");
        }
    }
    else if (target == "v")
    {
        step = (std::abs(stepType) == 1) ? m_tuneSettings.fineTuneVStep1 :m_tuneSettings.fineTuneVStep2;
        if(stepType < 0) step = -step;
        // 1.计算期望的最新位置
        double desiredV = m_hiddenV - step;
        // 2.限定调整范围
        double minBound = m_initialV - m_tuneSettings.fineTuneVStep2 * 10;
        double maxBound = m_initialV + m_tuneSettings.fineTuneVStep2 * 10;
        m_hiddenV = std::max(minBound,std::min(desiredV, maxBound));
        // 3.将真实偏差映射给拉杆显示
        int vOffset = static_cast<int>(std::round((m_initialV - m_hiddenV) * 1000.0));
        m_sliderV->setValue(vOffset);
        // 如果到底了给出警告
        if(desiredV < minBound || desiredV > maxBound)
        {
            handleLogMessage("<span style='color: #D13438;'>提示：已达到 V 单次最大调节上限!</span>");
        }
    }
    m_paramTable->item(row, 1)->setText(QString::number(m_hiddenPosition, 'f', 0));
    m_paramTable->item(row, 4)->setText(QString::number(m_hiddenV, 'f', 3));
}

// 精调按钮触发
void AutoTuningWidget::onStartFineTuneClicked()
{
    // 开始精确调参前，k，b，R归零
    m_currentK = 0;
    m_currentB = 0;
    m_currentR = 0;

    int row = m_cmbFineTuneMz->currentData().toInt();

    if (row < 0 || !m_paramTable->item(row, 2) || !m_paramTable->item(row, 3)) {
        handleLogMessage("错误：参数表格异常，无法开始精调！");
        return;
    }

    m_btnStartFineTune->setEnabled(false);
    m_btnStartFineTune->setText("⏳ 精调中...");
    m_btnStopFineTune->setEnabled(true);
    handleLogMessage("更新参数并开始精确调参...");

    // 从表格读取数据，并进行有效性校验
    double position = 0.0, v = 0.0, gain = 0.0, offset = 0.0;
    if (!readDoubleFromTable(m_paramTable, row, 1, position, "Position") ||
        !readDoubleFromTable(m_paramTable, row, 4, v, "V") ||
        !readDoubleFromTable(m_paramTable, 0, 2, gain, "全局 Gain") ||
        !readDoubleFromTable(m_paramTable, 0, 3, offset, "全局 Offset")) {
        m_btnStartFineTune->setEnabled(true);
        m_btnStartFineTune->setText("▶ 开始精调");
        m_btnStopFineTune->setEnabled(false);
        return;
    }

    m_hiddenPosition = position;
    m_hiddenV = v;

    // 把当前的表格值作为最新原点，归零拉杆
    m_initialPosition = m_hiddenPosition;
    m_initialV = m_hiddenV;
    m_sliderPos->setValue(0);
    m_sliderV->setValue(0);

    // 更新表格
    // 重新计算衍生控制参数
    double off_dac = offset + m_hiddenV;
    double off_code = off_dac * TuningConstants::DacSlope + TuningConstants::DacOffset;
    double reso = TuningConstants::ResoFactor * m_hiddenPosition * gain + off_code;
    // 全面回写表格（补齐之前遗漏的 Reso, DAC, Code 列）
    m_paramTable->item(row, 1)->setText(QString::number(m_hiddenPosition, 'f', 0));
    m_paramTable->item(row, 4)->setText(QString::number(m_hiddenV, 'f', 3));
    m_paramTable->item(row, 5)->setText(QString::number(reso, 'f', 1));
    m_paramTable->item(row, 6)->setText(QString::number(off_dac, 'f', 3));
    m_paramTable->item(row, 7)->setText(QString::number(off_code, 'f', 1));

    InstrumentParams params;
    params.position = m_hiddenPosition;
    params.V = m_hiddenV;
    params.gain = gain;
    params.offset = offset;

    bool mzOk = false;
    double targetMz = m_cmbFineTuneMz->currentText().trimmed().toDouble(&mzOk);
    if (!mzOk) {
        handleLogMessage("<span style='color: #D13438;'>错误：目标 m/z 无效！</span>");
        m_btnStartFineTune->setEnabled(true);
        m_btnStartFineTune->setText("▶ 开始精调");
        m_btnStopFineTune->setEnabled(false);
        return;
    }


    // 计算局部扫描的起点和终点，并发送信号
    int startMz = static_cast<int>(targetMz - TuningConstants::FineTuneScanWindow);
    int endMz = static_cast<int>(targetMz + TuningConstants::FineTuneScanWindow);
    emit sSetScanRange(startMz, endMz);
    m_localPlot->xAxis->setRange(startMz + 0.5, endMz - 0.5);
    m_localPlot->replot();
    //在下发精调指令前，检查子线程是否启动
    if (!m_workerThread || !m_workerThread->isRunning()) {
        setupThread();
    }
    // ⚠️ 【硬件交互】: 调谐前开启 V5 阀门 (需要底层提供接口，假设为 setValveEnabled)
    m_hardware->setValveEnabled(true);
    m_hardware->startInstrument();

    QMetaObject::invokeMethod(m_worker, "doFineTuneScan", Qt::QueuedConnection,
                              Q_ARG(double, targetMz),
                              Q_ARG(InstrumentParams, params),
                              Q_ARG(TuneSettings, m_tuneSettings));
}
// 停止精调
void AutoTuningWidget::onStopFineTuneClicked()
{
    handleLogMessage("=== 收到精调强制停止指令，正在停止... ===");
    if (m_worker) {
        m_worker->forceStop();
    }
    m_hardware->stopInstrument();
    emit controlScan(false);
    if (m_btnStopFineTune) {
        m_btnStopFineTune->setEnabled(false);
        m_btnStopFineTune->setText("⏳ 停止中...");
    }
    // ⚠️ 【硬件交互】: 调谐结束关闭 V5 阀门
    m_hardware->setValveEnabled(false);
}
void AutoTuningWidget::handleFineTuneResult(bool found, double centerMz, double width, double height, QVector<double> x, QVector<double> y) {
    if (!x.isEmpty() && !y.isEmpty())
    {
        // 1. 同步更新 UI 线程的数据仓库
        m_cachedXData = x;
        m_cachedYData = y;

        // 将单峰精调的数据也打包并入统一的中央缓存字典
        double currentTargetMz = m_cmbFineTuneMz->currentText().toDouble();
        PeakResult res = {found, currentTargetMz, centerMz, height, width};
        m_peakResultCache[currentTargetMz] = res;

        refreshLocalPlot();

        m_customPlot->graph(0)->setData(x, y);
        if (!x.isEmpty()) {
            double minX = currentTargetMz - 3; // 扫描起点
            double maxX = currentTargetMz + 3;  // 扫描终点

            // 让 X 轴紧贴实际扫描范围，并在右侧多留出 2% 的视觉留白余量
            double margin = (maxX - minX) * 0.02;
            m_customPlot->xAxis->setRange(minX, maxX + margin);
        }
        m_customPlot->graph(0)->rescaleValueAxis();
        m_customPlot->yAxis->scaleRange(1.1, m_customPlot->yAxis->range().center());
        m_customPlot->replot(QCustomPlot::rpQueuedReplot);

        double pos = m_lblCurrentPos->text().toDouble();
        double v = m_lblCurrentV->text().toDouble();
        handleLogMessage(QString("本次精调完成！ m/z:%1 | Position:%2 | V:%3").arg(centerMz).arg(pos).arg(v));
    }
    // UI 接收到结果，代表本次微调取图结束，安全停止仪器
    m_hardware->stopInstrument();
    emit controlScan(false);

    int endMz = getMaxMz();
    emit sSetScanRange(0,endMz);

    m_btnStartFineTune->setEnabled(true);
    m_btnStartFineTune->setText("▶ 开始精调");

    if (m_btnStopFineTune) {
        m_btnStopFineTune->setEnabled(false);
        m_btnStopFineTune->setText("🛑 停止");
    }
}
// 刷新局部精调图表的显示
void AutoTuningWidget::refreshLocalPlot() {
    if (m_cachedXData.isEmpty() || m_cachedYData.isEmpty()) return;

    double targetMz = m_cmbFineTuneMz->currentText().toDouble();

    // 设置局部视图坐标范围
    m_localPlot->xAxis->setRange(targetMz - 2.5, targetMz + 2.5);
    m_localPlot->graph(0)->setData(m_cachedXData, m_cachedYData);
    m_localPlot->graph(0)->rescaleValueAxis();
    m_localPlot->yAxis->scaleRange(1.1, m_localPlot->yAxis->range().center());
    m_localPlot->replot();

    // 从中央缓存字典中，直接提取当前选中质量数的寻峰数据进行映射
    if (m_peakResultCache.contains(targetMz)) {
        PeakResult res = m_peakResultCache[targetMz];
        if (res.found) {
            m_lblCurrentPos->setText(QString::number(res.centerMz, 'f', 3));
            m_lblCurrentV->setText(QString::number(res.width, 'f', 3));
            m_lblPeakHeight->setText(QString("峰  强: %1").arg(res.height, 0, 'e', 2));
        } else {
            m_lblCurrentPos->setText("未找到");
            m_lblCurrentV->setText("-");
            m_lblPeakHeight->setText("峰  强: -");
        }
    } else {
        // 如果该质量数尚未进行过任何调谐分析，显示待机占位符
        m_lblCurrentPos->setText("-");
        m_lblCurrentV->setText("-");
        m_lblPeakHeight->setText("峰  强: -");
    }
}

// 统一调整：开始精调
void AutoTuningWidget::onStartFineTuneUnifiedScan(QList<double> targetMasses, QList<int> paramTableRows)
{
    if (targetMasses.isEmpty() || paramTableRows.isEmpty()) {
        handleLogMessage("错误：统一精调目标为空！");
        m_fineTuneWgt->onUnifiedScanFinished();
        return;
    }
    if (targetMasses.size() != paramTableRows.size()) {
        handleLogMessage("错误：统一精调目标质量数与参数表行数不匹配！");
        m_fineTuneWgt->onUnifiedScanFinished();
        return;
    }

    double gain = 0.0, offset = 0.0;
    if (!readDoubleFromTable(m_paramTable, 0, 2, gain, "全局 Gain") ||
        !readDoubleFromTable(m_paramTable, 0, 3, offset, "全局 Offset")) {
        m_fineTuneWgt->onUnifiedScanFinished();
        return;
    }

    QList<InstrumentParams> paramsList;

    for (int i = 0; i < paramTableRows.size(); ++i) {
        int row = paramTableRows[i];
        if (row < 0 || row >= m_paramTable->rowCount()) {
            handleLogMessage(QString("错误：统一精调参数表第 %1 行越界！").arg(row));
            m_fineTuneWgt->onUnifiedScanFinished();
            return;
        }

        double pos = 0.0, v = 0.0;
        if (!readDoubleFromTable(m_paramTable, row, 1, pos, QString("Position (行%1)").arg(row)) ||
            !readDoubleFromTable(m_paramTable, row, 4, v, QString("V (行%1)").arg(row))) {
            m_fineTuneWgt->onUnifiedScanFinished();
            return;
        }

        double off_dac = offset + v;
        double off_code = off_dac * TuningConstants::DacSlope + TuningConstants::DacOffset;
        double reso = TuningConstants::ResoFactor * pos * gain + off_code;

        m_paramTable->item(row, 1)->setText(QString::number(pos, 'f', 0));
        m_paramTable->item(row, 4)->setText(QString::number(v, 'f', 3));
        m_paramTable->item(row, 5)->setText(QString::number(reso, 'f', 1));
        m_paramTable->item(row, 6)->setText(QString::number(off_dac, 'f', 3));
        m_paramTable->item(row, 7)->setText(QString::number(off_code, 'f', 1));

        InstrumentParams params;
        params.position = pos;
        params.V = v;
        params.gain = gain;
        params.offset = offset;
        paramsList.append(params);
    }

    // 设置覆盖所有目标质量数的扫描范围，并留余量
    double minMz = *std::min_element(targetMasses.begin(), targetMasses.end());
    double maxMz = *std::max_element(targetMasses.begin(), targetMasses.end());
    int startMz = qMax(0, static_cast<int>(minMz - TuningConstants::FineTuneUnifiedMargin));
    int endMz = static_cast<int>(maxMz + TuningConstants::FineTuneUnifiedMargin);

    emit sSetScanRange(startMz, endMz);
    m_localPlot->xAxis->setRange(startMz, endMz);
    m_localPlot->replot();

    handleLogMessage(QString("统一精调启动：扫描范围 %1 - %2，目标数 %3")
                     .arg(startMz).arg(endMz).arg(targetMasses.size()));

    if (!m_workerThread || !m_workerThread->isRunning()) {
        setupThread();
    }
    m_hardware->setValveEnabled(true);
    m_hardware->startInstrument();

    QMetaObject::invokeMethod(m_worker, "doFineTuneUnifiedScan", Qt::QueuedConnection,
                              Q_ARG(QList<double>, targetMasses),
                              Q_ARG(QList<InstrumentParams>, paramsList),
                              Q_ARG(TuneSettings, m_tuneSettings));
}

// 统一调整：停止精调
void AutoTuningWidget::onStopFineTuneUnifiedScan()
{
    handleLogMessage("=== 收到统一精调强制停止指令，正在停止... ===");
    if (m_worker) {
        m_worker->forceStop();
    }
    m_hardware->stopInstrument();
    emit controlScan(false);
    m_hardware->setValveEnabled(false);
    m_fineTuneWgt->onUnifiedScanFinished();
}

// 统一调整：接收扫描结果
void AutoTuningWidget::handleFineTuneUnifiedResult(QVector<PeakResult> results, QVector<double> xData, QVector<double> yData)
{
    // 1. 刷新统一调整表格
    for (int i = 0; i < results.size(); ++i) {
        const PeakResult& res = results[i];
        if (res.found) {
            m_fineTuneWgt->updatePeakResult(i, res.height, res.centerMz, res.width);
            int row = findCheckedValidIndex(res.targetMz, 0);
            double currentPosition = m_paramTable->item(row, 1)->text().toDouble();
            double targetPosition = currentPosition * res.centerMz / res.targetMz;
            m_fineTuneWgt->getPosAdjustData(i, targetPosition);
            handleLogMessage(QString("统一精调结果：目标m/z %1 | 实际出峰 %2 | 峰宽 %3 | 峰强 %4")
                             .arg(res.targetMz)
                             .arg(res.centerMz, 0, 'f', 3)
                             .arg(res.width, 0, 'f', 3)
                             .arg(res.height, 0, 'e', 2));
        } else {
            m_fineTuneWgt->updatePeakResult(i, 0.0, 0.0, 0.0);
            m_fineTuneWgt->getPosAdjustData(i, 0);
            handleLogMessage(QString("<span style='color: #D13438;'>统一精调未找到目标m/z: %1</span>").arg(res.targetMz));
        }
    }

    // 2. 在主谱图上显示本次扫描到的谱图
    if (!xData.isEmpty() && !yData.isEmpty()) {
        m_customPlot->graph(0)->setData(xData, yData);
        if (!xData.isEmpty()) {
            double minX = xData.first();
            double maxX = xData.last();
            double margin = (maxX - minX) * 0.02;
            m_customPlot->xAxis->setRange(minX - margin, maxX + margin);
        }
        m_customPlot->graph(0)->rescaleValueAxis();
        m_customPlot->yAxis->scaleRange(1.1, m_customPlot->yAxis->range().center());
        m_customPlot->replot(QCustomPlot::rpQueuedReplot);
    }

    // 3. 收尾
    m_hardware->stopInstrument();
    emit controlScan(false);
    emit sSetScanRange(0, getMaxMz());

    m_fineTuneWgt->onUnifiedScanFinished();
}

void AutoTuningWidget::loadDefaultSettings()
{
    if(m_configFilePath.isEmpty()) return;
    QString jsonPath = m_configFilePath + "/params.json";

    if(!QFile::exists(jsonPath)) return;
    QVariantMap loadDefaultMap ;
    if(loadDefaultMap.isEmpty()) return;

    m_tuneSettings.maxIterations = loadDefaultMap["maxIterations"].toInt();
    m_tuneSettings.rsdTarget = loadDefaultMap["rsdTarget"].toDouble();
    m_tuneSettings.peakWidthLower = loadDefaultMap["peakWidthLower"].toDouble();
    m_tuneSettings.peakWidthUpper = loadDefaultMap["peakWidthUpper"].toDouble();
    m_tuneSettings.scanCountPerEval = loadDefaultMap["scanCountPerEval"].toInt();
    m_tuneSettings.averageMaxLength = loadDefaultMap["averageMaxLength"].toInt();
    m_tuneSettings.peakWidthRatio = loadDefaultMap["peakWidthRatio"].toDouble();
    m_tuneSettings.peakWidthRatio_1 = loadDefaultMap["peakWidthRatio_1"].toDouble();
    m_tuneSettings.positionCrvEnabled = loadDefaultMap["positionCrvEnabled"].toBool();
    m_tuneSettings.positionCrv_R2 = loadDefaultMap["positionCrv_R2"].toDouble();
    m_tuneSettings.gainStep = loadDefaultMap["gainStep"].toDouble();
    m_tuneSettings.offsetStep = loadDefaultMap["offsetStep"].toDouble();
    m_tuneSettings.minPeakHeight = loadDefaultMap["minPeakHeight"].toDouble();
    m_tuneSettings.peakSearchWindow = loadDefaultMap["peakSearchWindow"].toDouble();
    m_tuneSettings.stretchLower = loadDefaultMap["stretchLower"].toDouble();
    m_tuneSettings.stretchUpper = loadDefaultMap["stretchUpper"].toDouble();
    m_tuneSettings.distancePenaltyWeight = loadDefaultMap["distancePenaltyWeight"].toDouble();
    m_tuneSettings.fineTunePosStep1 = loadDefaultMap["fineTunePosStep1"].toDouble();
    m_tuneSettings.fineTunePosStep2 = loadDefaultMap["fineTunePosStep2"].toDouble();
    m_tuneSettings.fineTuneVStep1 = loadDefaultMap["fineTuneVStep1"].toDouble();
    m_tuneSettings.fineTuneVStep2 = loadDefaultMap["fineTuneVStep2"].toDouble();
    m_tuneSettings.elecR2Target = loadDefaultMap["elecR2Target"].toDouble();
    m_tuneSettings.elecR2Min = loadDefaultMap["elecR2Min"].toDouble();
    m_tuneSettings.elecIconStep = loadDefaultMap["elecIconStep"].toInt();
    m_tuneSettings.elecMcpStep = loadDefaultMap["elecMcpStep"].toInt();
    m_tuneSettings.elecMaxCount = loadDefaultMap["elecMaxCount"].toInt();
    m_tuneSettings.elecSmallMzThreshold = loadDefaultMap["elecSmallMzThreshold"].toDouble();
	m_tuneSettings.elecGainStep = loadDefaultMap["elecGainStep"].toDouble();
    m_tuneSettings.elecOffsetStep = loadDefaultMap["elecOffsetStep"].toDouble();

    handleLogMessage("已成功载入默认参数配置表！");
}

//-------------------------------------------
//信息日志输出
/*!
 * \brief AutoTuningWidget::handleLogMessage
 * \param msg
 * \param lightShow 是否在高亮显眼位置显示
 * \param level 日志等级
 */
void AutoTuningWidget::handleLogMessage(const QString &msg, const bool &lightShow, const int &level)
{
    QString timeStr = "";
    if (msg.startsWith(" [成功]") || msg.startsWith(" [失败]") || msg.startsWith("---")) {
        timeStr = QDateTime::currentDateTime().toString("[hh:mm:ss] ");
    }
    QString finalMsg = QString("<span style='color: #333333;'> %1%2</span>").arg(timeStr).arg(msg);
    m_logTextEdit->append(finalMsg);

    if(lightShow || level > 0){
        m_lbInfo->setText(finalMsg);
    }
}
//质谱图
void AutoTuningWidget::handlePlotDataReady(const QVector<double> &x, const QVector<double> &y)
{
    m_cachedXData = x;
    m_cachedYData = y;

    m_customPlot->graph(0)->setData(x, y);
    if (!x.isEmpty()) {
        double minX = x.first(); // 扫描起点
        double maxX = x.last();  // 扫描终点

        // 让 X 轴紧贴实际扫描范围，并在右侧多留出 2% 的视觉留白余量
        double margin = (maxX - minX) * 0.02;
        m_customPlot->xAxis->setRange(minX, maxX + margin);
    }
    m_customPlot->graph(0)->rescaleValueAxis();
    m_customPlot->yAxis->scaleRange(1.1, m_customPlot->yAxis->range().center());
    m_customPlot->replot(QCustomPlot::rpQueuedReplot);

    // 尝试更新局部精调视图（如果当前正在看某个峰）
    refreshLocalPlot();
}
// 处理自动调谐周期内发回的多峰分析结果
void AutoTuningWidget::handleAutoTunePeaksReady(QVector<PeakResult> results)
{
    for (const auto& res : results) {
        m_peakResultCache[res.targetMz] = res;
    }
}
// 根据底层发来的 8 个参数更新表格指定行
void AutoTuningWidget::handleTableUpdate(int row, double mz, double pos, double gain, double offset, double V, double reso, double off_dac, double off_code)
{
    m_paramTable->blockSignals(true);

    //chk功能新增后，需要按mz检索，算法中传入的是从0开始的
    row = findCheckedValidIndex(mz,0);

    QTableWidgetItem* itemMz = new QTableWidgetItem(QString::number(mz, 'f', 1));
    itemMz->setForeground(QBrush(QColor(0, 150, 255)));
    itemMz->setData(Qt::UserRole, QString::number(mz, 'f', 1));
    m_paramTable->setItem(row, 0, itemMz);
    m_paramTable->setItem(row, 1, new QTableWidgetItem(QString::number(pos, 'f', 1)));
    {
        m_paramTable->setItem(0, 2, new QTableWidgetItem(QString::number(gain, 'f', 3)));
        m_paramTable->setItem(0, 3, new QTableWidgetItem(QString::number(offset, 'f', 3)));
        m_paramTable->item(0, 2)->setTextAlignment(Qt::AlignCenter);
        m_paramTable->item(0, 3)->setTextAlignment(Qt::AlignCenter);
    }
    m_paramTable->setItem(row, 4, new QTableWidgetItem(QString::number(V, 'f', 3)));

    QTableWidgetItem* itemReso = new QTableWidgetItem(QString::number(reso, 'f', 1));
    itemReso->setForeground(QBrush(QColor(220, 50, 50)));
    m_paramTable->setItem(row, 5, itemReso);

    QTableWidgetItem* itemDAC = new QTableWidgetItem(QString::number(off_dac, 'f', 3));
    itemDAC->setForeground(QBrush(QColor(220, 50, 50)));
    m_paramTable->setItem(row, 6, itemDAC);

    QTableWidgetItem* itemCode = new QTableWidgetItem(QString::number(off_code, 'f', 1));
    itemCode->setForeground(QBrush(QColor(220, 50, 50)));
    m_paramTable->setItem(row, 7, itemCode);

    for(int r=0; r<m_paramTable->rowCount(); ++r){
        QTableWidgetItem* itemMZ = m_paramTable->item(r,0);
        if(!itemMZ || itemMZ->text().trimmed().isEmpty()) continue;
        double mz = itemMZ->text().toDouble();
        double tempPos = m_paramTable->item(r, 1)->text().toDouble();
        if(m_currentK > 1e-5){
            tempPos = m_currentK * mz +m_currentB;
            if(!m_paramTable->item(r, 1)){
                m_paramTable->setItem(r, 1, new QTableWidgetItem(""));
            }
            m_paramTable->item(r, 1)->setText(QString::number(tempPos, 'f', 1));
        }
        double currentV = 0.0;
        if(m_paramTable->item(r, 4) && !m_paramTable->item(r, 4)->text().isEmpty()){
            currentV = m_paramTable->item(r, 4)->text().toDouble();
        }else{
            if(!m_paramTable->item(r, 4)){
                m_paramTable->setItem(r, 4, new QTableWidgetItem("0.000"));
            }
            else{
                m_paramTable->item(r, 4)->setText("0.000");
            }
        }
        double tempOff_dac = offset + currentV;
        double tempOff_code = tempOff_dac * 6553.5 + 32767.5;
        double tempReso = 0.5 * tempPos * gain + tempOff_code;

        if(!m_paramTable->item(r, 5)){
            m_paramTable->setItem(r, 5, new QTableWidgetItem(""));
        }
        m_paramTable->item(r, 5)->setText(QString::number(tempReso, 'f', 1));
        m_paramTable->item(r, 5)->setForeground(QBrush(QColor(220, 50, 50)));

        if(!m_paramTable->item(r, 6)){
            m_paramTable->setItem(r, 6, new QTableWidgetItem(""));
        }
        m_paramTable->item(r, 6)->setText(QString::number(tempOff_dac, 'f', 3));
        m_paramTable->item(r, 6)->setForeground(QBrush(QColor(220, 50, 50)));

        if(!m_paramTable->item(r, 7)){
            m_paramTable->setItem(r, 7, new QTableWidgetItem(""));
        }
        m_paramTable->item(r, 7)->setText(QString::number(tempOff_code, 'f', 1));
        m_paramTable->item(r, 7)->setForeground(QBrush(QColor(220, 50, 50)));
    }

    // 保留原有的生效勾选状态
    bool enabled = false;
    QTableWidgetItem* oldCheck = m_paramTable->item(row, 8);
    if (oldCheck) enabled = (oldCheck->checkState() == Qt::Checked);
    m_paramTable->setItem(row, 8, createCheckItem(enabled));

    m_paramTable->blockSignals(false);
}
//完成
void AutoTuningWidget::handleWorkFinished()
{
    m_hardware->stopInstrument();   //打断获取数据的死循环
    emit controlScan(false);        //停止扫描
    // ⚠️ 【硬件交互】: 调谐结束关闭 V5 阀门
    m_hardware->setValveEnabled(false);
    if(m_workerThread->isRunning()) //完成后退出子线程
    {
        m_workerThread->quit();
    }
    m_btnTuneGain->setEnabled(true);
    m_btnTuneOffset->setEnabled(true);
    m_btnTunePosition->setEnabled(true);
    m_btnStartFineTune->setEnabled(true);
    m_btnTuneGain->setText("⏩调节Gain");
    m_btnTuneOffset->setText("⏩调节偏移");
    m_btnTunePosition->setText("⏩调节Position");

    m_btnStartExec->setEnabled(true);
    m_btnStartExec->setText("开始执行");
    m_btnForceStop->setEnabled(false);
    m_btnForceStop->setText("强制停止");

    // 将停止按钮的样式和文字还原为最初状态
    m_btnStopDebug->setEnabled(false);
    m_btnStopDebug->setText("🛑 停止");
    m_btnStopDebug->setStyleSheet(R"(
                                  QPushButton { background-color: #D13438; color: white; border-radius: 6px; border: none; font-weight: bold; }
                                  QPushButton:hover { background-color: #B3272B; }
                                  QPushButton:pressed { background-color: #8E1F22; padding-top: 2px; }
                                  QPushButton:disabled { background-color: #E8999B; color: #F0F0F0; }
                                  )");

    handleLogMessage(">>> 流程已完全结束，系统处于待机状态。");
}

// 【新增】自动备份参数到临时文件
void AutoTuningWidget::handleAutoBackupParams(QList<double> targetMasses, QList<InstrumentParams> currentParams, int tuneState)
{
    if (m_configFilePath.isEmpty() || !QDir(m_configFilePath).exists()) {
        return;  // 路径无效，静默跳过
    }
    
    // 备份文件路径
    QString backupPath = m_configFilePath + "/auto_tune_backup.json";
    
    QVariantMap backupMap;
    
    // 保存备份时间戳
    backupMap["backupTime"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    backupMap["tuneState"] = tuneState;  // 0=Gain, 1=Offset, 2=Position
    
    // 保存参数表格
    QVariantList tableLst;
    for (int i = 0; i < targetMasses.size(); ++i) {
        QVariantMap rowMap;
        rowMap["mz"] = targetMasses[i];
        rowMap["position"] = QString::number(currentParams[i].position, 'f', 0);
        rowMap["gain"] = QString::number(currentParams[i].gain, 'f', 6);
        rowMap["offset"] = QString::number(currentParams[i].offset, 'f', 6);
        rowMap["v"] = QString::number(currentParams[i].V, 'f', 3);
        
        // 计算衍生参数
        double off_dac = currentParams[i].offset + currentParams[i].V;
        double off_code = off_dac * TuningConstants::DacSlope + TuningConstants::DacOffset;
        double reso = TuningConstants::ResoFactor * currentParams[i].position * currentParams[i].gain + off_code;
        
        rowMap["reso"] = QString::number(reso, 'f', 1);
        rowMap["dac"] = QString::number(off_dac, 'f', 3);
        rowMap["code"] = QString::number(off_code, 'f', 1);
        
        tableLst.append(rowMap);
    }
    backupMap["paramsTable"] = tableLst;
    
    // 写入备份文件
    handleLogMessage(QString("<span style='color: #107C10;'>💾 自动备份成功: %1</span>").arg(backupPath));
}

// 【新增】从备份文件恢复参数
void AutoTuningWidget::restoreFromBackup()
{
//    if (m_configFilePath.isEmpty() || !QDir(m_configFilePath).exists()) {
//        return;
//    }
    
//    QString backupPath = m_configFilePath + "/auto_tune_backup.json";
//    if (!QFile::exists(backupPath)) {
//        return;  // 没有备份文件
//    }
    
//    QVariantMap backupMap = cJsonFileOperate::jsonFile2QVarint(backupPath).toMap();
//    if (backupMap.isEmpty()) {
//        return;  // 读取失败
//    }
    
//    // 检查备份时间
//    QString backupTime = backupMap["backupTime"].toString();
//    if (backupTime.isEmpty()) {
//        return;
//    }
    
//    // 提示用户是否恢复
//    QMessageBox::StandardButton reply = QMessageBox::question(
//        this,
//        "检测到调谐备份",
//        QString("发现上次自动调谐的备份参数（%1）\n是否恢复到表格？").arg(backupTime),
//        QMessageBox::Yes | QMessageBox::No,
//        QMessageBox::Yes
//    );
    
//    if (reply == QMessageBox::No) {
//        return;  // 用户取消
//    }
    
//    // 恢复参数到表格
//    QVariantList tableLst = backupMap["paramsTable"].toList();
//    if (tableLst.isEmpty()) {
//        handleLogMessage("<span style='color: #D13438;'>恢复失败：备份数据为空</span>");
//        return;
//    }
    
//    // 清空当前表格
//    m_paramTable->setRowCount(0);
//    m_paramTable->blockSignals(true);
//    m_cmbFineTuneMz->blockSignals(true);
//    m_cmbFineTuneMz->clear();
    
//    // 填充备份数据
//    for (int row = 0; row < tableLst.count(); ++row) {
//        QVariantMap rowMap = tableLst[row].toMap();
        
//        m_paramTable->insertRow(row);
        
//        // m/z 列（蓝色）
//        QString mzStr = rowMap["mz"].toString();
//        QTableWidgetItem* itemMz = new QTableWidgetItem(mzStr);
//        itemMz->setForeground(QBrush(QColor(0, 150, 255)));
//        itemMz->setData(Qt::UserRole, mzStr);
//        m_paramTable->setItem(row, 0, itemMz);
        
//        // 其他列
//        m_paramTable->setItem(row, 1, new QTableWidgetItem(rowMap["position"].toString()));
//        m_paramTable->setItem(row, 2, new QTableWidgetItem(rowMap["gain"].toString()));
//        m_paramTable->setItem(row, 3, new QTableWidgetItem(rowMap["offset"].toString()));
//        m_paramTable->setItem(row, 4, new QTableWidgetItem(rowMap["v"].toString()));
        
//        QTableWidgetItem* itemReso = new QTableWidgetItem(rowMap["reso"].toString());
//        itemReso->setForeground(QBrush(QColor(220, 50, 50)));
//        m_paramTable->setItem(row, 5, itemReso);
        
//        QTableWidgetItem* itemDac = new QTableWidgetItem(rowMap["dac"].toString());
//        itemDac->setForeground(QBrush(QColor(220, 50, 50)));
//        m_paramTable->setItem(row, 6, itemDac);
        
//        QTableWidgetItem* itemCode = new QTableWidgetItem(rowMap["code"].toString());
//        itemCode->setForeground(QBrush(QColor(220, 50, 50)));
//        m_paramTable->setItem(row, 7, itemCode);

//        // 恢复后默认不勾选
//        m_paramTable->setItem(row, 8, createCheckItem());
//    }
    
//    handleLogMessage(QString("<span style='color: #107C10;'>✅ 已成功从备份恢复 %1 组参数</span>").arg(tableLst.count()));
    
//    m_paramTable->blockSignals(false);
//    refreshFineTuneMzCombo();

//    // 可选：恢复后删除备份文件
//    // QFile::remove(backupPath);
}

// 获取表格参数
QVariantList AutoTuningWidget::getTableData(bool onlyEnabled)
{
    QVariantList resList;
    int count = m_paramTable->rowCount();
    for (int i = 0;i < count;i++)
    {
        QVariantMap rowMap;
        QTableWidgetItem* item = m_paramTable->item(i,0);
        if (!item || item->text().trimmed().isEmpty())  //跳过空行
        {
            continue;
        }
        if (onlyEnabled && !isRowEnabled(m_paramTable, i))  // 跳过未勾选的行
        {
            continue;
        }
        QString mzStr = item->text();
        if(mzStr == ""){
            continue;
        }
        rowMap["mz"] = mzStr;

        // 【P2修复】增强所有 item 访问的空指针防护
        item = m_paramTable->item(i,1);
        rowMap["position"] = item ? item->text() : "0";

        item = m_paramTable->item(0,2);
        rowMap["gain"] = item ? item->text() : "0";

        item = m_paramTable->item(0,3);
        rowMap["offset"] = item ? item->text() : "0";

        item = m_paramTable->item(i,4);
        rowMap["v"] = item ? item->text() : "0";

        item = m_paramTable->item(i,5);
        rowMap["reso"] = item ? item->text() : "0";

        item = m_paramTable->item(i,6);
        rowMap["dac"] = item ? item->text() : "0";

        item = m_paramTable->item(i,7);
        rowMap["code"] = item ? item->text() : "0";

        // 拟合曲线的k、b、R
        rowMap["k"] = QString::number(m_currentK, 'f', 6);
        rowMap["b"] = QString::number(m_currentB, 'f', 6);
        rowMap["R"] = QString::number(m_currentR, 'f', 6);

        resList.append(rowMap);
    }
    return resList;
}

/*!
 * \brief AutoTuningWidget::getMaxMz 获取最大的mz，适当的扩大，用于恢复全扫的起止mz
 * \return
 */
int AutoTuningWidget::getMaxMz()
{
    int res = 60;
    QVariantList resList = getTableData(true);
    if(resList.count() > 0){
        double tmp = 60.0;
        QVariantMap rowMap = resList[resList.count() - 1].toMap();
        tmp = rowMap["mz"].toDouble();
        if(tmp > 0){
            tmp = tmp  + 15;

            res = static_cast<int>(tmp);
        }
    }

    return res;

}

QVariantList AutoTuningWidget::getTableEleData(bool onlyEnabled)
{
    QVariantList resList;
    int count = m_elTable->rowCount();
    for (int i = 0;i < count;i++)
    {
        QVariantMap rowMap;
        QTableWidgetItem* item = m_elTable->item(i,0);
        if (!item || item->text().trimmed().isEmpty())  //跳过空行
        {
            continue;
        }
        if (onlyEnabled && !isRowEnabled(m_elTable, i))  // 跳过未勾选的行
        {
            continue;
        }
        QString mzStr = item->text();
        if(mzStr == ""){
            continue;
        }
        rowMap["mz"] = mzStr;

        // 【P2修复】增强所有 item 访问的空指针防护
        item = m_elTable->item(i,1);
        rowMap["position"] = item ? item->text() : "0";

        item = m_elTable->item(0,2);
        rowMap["gain"] = item ? item->text() : "0";

        item = m_elTable->item(0,3);
        rowMap["offset"] = item ? item->text() : "0";

        item = m_elTable->item(i,4);
        rowMap["v"] = item ? item->text() : "0";

        item = m_elTable->item(i,5);
        rowMap["reso"] = item ? item->text() : "0";

        item = m_elTable->item(i,6);
        rowMap["dac"] = item ? item->text() : "0";

        item = m_elTable->item(i,7);
        rowMap["code"] = item ? item->text() : "0";

        item = m_elTable->item(i,8);
        rowMap["DestInten"] = item ? item->text() : "0";

        // 拟合曲线的k、b、R
        rowMap["k"] = QString::number(m_currentK, 'f', 6);
        rowMap["b"] = QString::number(m_currentB, 'f', 6);
        rowMap["R"] = QString::number(m_currentR, 'f', 6);

        resList.append(rowMap);
    }
    return resList;
}

/*!
 * \brief AutoTuningWidget::onSyncParamClicked 同步m_paramtable的参数
 */
void AutoTuningWidget::onSyncParamClicked()
{
    m_elTable->setRowCount(0); // 清空

    // 1. 遍历原始基础表格，按格式拷贝
    for (int r = 0; r < m_paramTable->rowCount(); ++r) {
        QTableWidgetItem* itemMz = m_paramTable->item(r, 0);
        if (!itemMz || itemMz->text().trimmed().isEmpty()) continue;

        int newRow = m_elTable->rowCount();
        m_elTable->insertRow(newRow);

        // 拷贝 m/z (保持蓝色渲染)
        QTableWidgetItem* newMzItem = new QTableWidgetItem(itemMz->text());
        newMzItem->setForeground(QBrush(QColor(0, 150, 255)));
        m_elTable->setItem(newRow, 0, newMzItem);

        // 拷贝 Position
        m_elTable->setItem(newRow, 1, new QTableWidgetItem(m_paramTable->item(r, 1)->text()));

        // 提取原表的全局 Gain 和 Offset 赋给 Adv 表，并设置文字居中
        QString globalGain = m_paramTable->item(0, 2)->text();
        QString globalOffset = m_paramTable->item(0, 3)->text();

        QTableWidgetItem* gainItem = new QTableWidgetItem(globalGain);
        gainItem->setTextAlignment(Qt::AlignCenter);
        m_elTable->setItem(newRow, 2, gainItem);

        QTableWidgetItem* offsetItem = new QTableWidgetItem(globalOffset);
        offsetItem->setTextAlignment(Qt::AlignCenter);
        m_elTable->setItem(newRow, 3, offsetItem);

        // 拷贝 V (常规黑字)
        m_elTable->setItem(newRow, 4, new QTableWidgetItem(m_paramTable->item(r, 4)->text()));

        // 拷贝 Reso, DAC, Code (保持红色渲染)
        for(int c = 5; c <= 7; ++c) {
            QTableWidgetItem* redItem = new QTableWidgetItem(m_paramTable->item(r, c)->text());
            redItem->setForeground(QBrush(QColor(220, 50, 50)));
            m_elTable->setItem(newRow, c, redItem);
        }

        // 初始化 DestInten (绿色渲染以示区别)
        QTableWidgetItem* itemInten = new QTableWidgetItem("1.0e-11");
        itemInten->setForeground(QBrush(QColor(0, 150, 0)));
        m_elTable->setItem(newRow, 8, itemInten);

        // 同步过来的行默认不勾选
        m_elTable->setItem(newRow, 9, createCheckItem());
    }

    // 2. 像 Auto 界面一样，在数据底部追加 1 行空白行备用，保持表格充实度一致
    int currentRows = m_elTable->rowCount();
    m_elTable->setRowCount(currentRows + 1);
    for(int i = currentRows; i < m_elTable->rowCount(); ++i) {
        for(int c = 0; c < 10; ++c) {
            m_elTable->setItem(i, c, new QTableWidgetItem(""));
        }
        m_elTable->setItem(i, 9, createCheckItem());
    }

    handleLogMessage("<span style='color: #0078D7;'>🔄 参数已成功从 Auto 面板同步至 Adv 面板。</span>");
}

/*!
 * \brief AutoTuningWidget::onStartExecClicked 开始执行
 */
void AutoTuningWidget::onStartExecClicked()
{
    m_currentK = 0;
    m_currentB = 0;
    m_currentR = 0;

    m_btnStartExec->setEnabled(false);
    m_btnStartExec->setText("⏳ 执行中...");
    m_btnForceStop->setEnabled(true);

    if(!m_workerThread) setupThread();

    QList<double> targetMasses;
    QList<double> DestIntens;
    QList<InstrumentParams> advParamsList;

    // 遍历 Adv 面板的 m_elTable
    for (int r = 0; r < m_elTable->rowCount(); ++r) {
        QTableWidgetItem* itemMz = m_elTable->item(r, 0);
        if (!itemMz || itemMz->text().trimmed().isEmpty()) continue;
        // 过滤未勾选的行
        if (!isRowEnabled(m_elTable, r)) continue;

        targetMasses.append(itemMz->text().toDouble());
        DestIntens.append(m_elTable->item(r, 8)->text().toDouble()); // 目标强度

        // 独立获取每一行的 Gain 和 Offset
        InstrumentParams p;
        p.position = m_elTable->item(r, 1)->text().toDouble();
        p.gain = m_elTable->item(r, 2)->text().toDouble();
        p.offset = m_elTable->item(r, 3)->text().toDouble();
        p.V = m_elTable->item(r, 4)->text().toDouble();
        advParamsList.append(p);
    }

    if (targetMasses.isEmpty()) {
        handleLogMessage("错误：Adv 表格无有效数据！");
        m_btnStartExec->setEnabled(true); m_btnForceStop->setEnabled(false);
        m_btnStartExec->setText("开始执行");
        return;
    }

    // 开启仪器
    m_hardware->startInstrument();
    // ⚠️ 【硬件交互】: 调谐前开启 V5 阀门 (需要底层提供接口，假设为 setValveEnabled)
    // m_hardware->setValveEnabled(true);
    int endMz = getMaxMz();
    emit sSetScanRange(0,endMz);    //设定初始扫描区域


    // 呼叫全新的靶向强度调谐线程
    QMetaObject::invokeMethod(m_worker, "doIntensityTargetTuning", Qt::QueuedConnection,
                              Q_ARG(QList<double>, targetMasses),
                              Q_ARG(QList<double>, DestIntens),
                              Q_ARG(QList<InstrumentParams>, advParamsList),
                              Q_ARG(TuneSettings, m_tuneSettings));
}

/*!
 * \brief AutoTuningWidget::onForceStopClicked 强制停止
 */
void AutoTuningWidget::onForceStopClicked()
{
    handleLogMessage("=== 收到拓展调谐停止指令... ===");
    if(m_worker) m_worker->forceStop();
    m_btnForceStop->setEnabled(false);
    m_btnForceStop->setText("⏳ 停止中...");

    m_hardware->stopInstrument();
    emit controlScan(false);
    // ⚠️ 【硬件交互】: 调谐结束关闭 V5 阀门
    m_hardware->setValveEnabled(false);
}

void AutoTuningWidget::applyParams()
{

}
//m/z与position拟合曲线绘制
void AutoTuningWidget::handleTrendlineReady(double k, double b, double rSquared, QVector<double> x, QVector<double> y)
{
    // 将子线程算好的 k、b、R缓存到 UI 主线程的变量中
    m_currentK = k;
    m_currentB = b;
    m_currentR = rSquared;

    // 1. 绘制实际测得的散点
    m_fitPlot->graph(0)->setData(x, y);

    // 2. 生成趋势线数据 (从 0 到 160 AMU)
    QVector<double> lineX, lineY;
    for(int i=0; i<=160; i+=10) {
        lineX.append(i);
        lineY.append(k * i + b);
    }
    m_fitPlot->graph(1)->setData(lineX, lineY);

    // 3. 自动调整轴范围并标注 R^2
    m_fitPlot->rescaleAxes();
    m_fitPlot->replot();

    m_fitPlot->clearItems();
    QCPItemText* textLabel = new QCPItemText(m_fitPlot);
    textLabel->setPositionAlignment(Qt::AlignTop | Qt::AlignLeft);
    textLabel->position->setType(QCPItemPosition::ptAxisRectRatio);
    textLabel->position->setCoords(0.05, 0.05);
    QString sign = b >= 0 ? "+" : "-";
    textLabel->setText(QString("Pos =%1 *mz%2 %3\nR²=%4").arg(k, 0, 'f', 4).arg(sign)
                       .arg(std::abs(b), 0, 'f', 4).arg(rSquared, 0, 'f', 4));
    textLabel->setFont(QFont("Arial", 12, QFont::Bold));
    textLabel->setPen(QPen(Qt::black));
    textLabel->setBrush(QBrush(QColor(255, 255, 255, 210)));
    m_fitPlot->replot();

    handleLogMessage(QString("拟合曲线更新： Pos =%1 *mz%2 %3\nR²=%4").arg(k, 0, 'f', 4).arg(sign)
                     .arg(std::abs(b), 0, 'f', 4).arg(rSquared, 0, 'f', 4));
}

// 获取参数变化
void AutoTuningWidget::handleChangeParam(InstrumentParams params, double reso, double offset_dac, double offset_code,
                                         bool startScan, bool isAutoCal)
{
    QVariantList dataLst = getTableData();
    if(dataLst.count() == 0){
        handleLogMessage(tr("参数表无有效的参数！不执行调整！"));
        return;
    }

    m_hardware->sendParameters();      //主要用于清空遗留的脏数据
    handleLogMessage(QString("参数更新：position:%1 | gain:%2 | offset:%3 | V:%4 |"
                             " reso:%5 | dac:%6 | code:%7")
                     .arg(params.position).arg(params.gain).arg(params.offset)
                     .arg(params.V).arg(reso).arg(offset_dac).arg(offset_code));
    // 停启扫描
    emit controlScan(false);

    // 等待事件循环处理完所有待处理的事件
    QCoreApplication::processEvents(QEventLoop::AllEvents, 500);

    // 或者使用 QEventLoop 等待特定条件
    QEventLoop loop;
    QTimer::singleShot(1000, &loop, &QEventLoop::quit);  // 最多等 1s
    loop.exec();

    //只有确认扫描真实停止了才进行修改 1s应该可以了

    //修改参数
    emit setParaRes(dataLst, isAutoCal);
    if(startScan) {
        QTimer::singleShot(1000, this, [this]() {
            emit controlScan(true);
        });
    }
}

// 获取电参数变化
void AutoTuningWidget::handleElecParam(int ep, int em, bool isScan)
{
    //停止扫描
    emit controlScan(false);
    //清空遗留数据
    m_hardware->sendParameters();
    handleLogMessage(QString(">>> 当前电参数：Icon:%1 | Mcp:%2").arg(ep).arg(em));
    // 等待事件循环处理完所有待处理的事件
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5000);

    // 或者使用 QEventLoop 等待特定条件
    QEventLoop loop;
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);  // 最多等 1s
    loop.exec();

    //只有确认扫描真实停止了才进行修改 1s应该可以了
    //电参数下发
    emit setElecPara(ep, em);
    //启动扫描
    if(isScan) {
        QTimer::singleShot(1000, this, [this]() {
            emit controlScan(true);
        });
    }
}
// 获取adv参数变化
void AutoTuningWidget::handleChangeAdvParam(InstrumentParams params, bool startScan)
{
    m_hardware->sendParameters();      //主要用于清空遗留的脏数据

    // 停启扫描
    emit controlScan(false);
    handleLogMessage(QString(">>> 当前基础参数：gain=%1 | offset=%2")
                     .arg(params.gain).arg(params.offset));

    // 等待事件循环处理完所有待处理的事件
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5000);

    // 或者使用 QEventLoop 等待特定条件
    QEventLoop loop;
    QTimer::singleShot(1000, &loop, &QEventLoop::quit);  // 最多等 1s
    loop.exec();

    //只有确认扫描真实停止了才进行修改 1s应该可以了
    QVariantList elParams = getTableEleData();
    //⚠️ 修改参数
    emit setParaRes(elParams,false);
    if(startScan) {
        QTimer::singleShot(5000, this, [this]() {
            emit controlScan(true);
        });
    }
}

void AutoTuningWidget::handleAdvTableUpdate(int row, double mz, double gain, double offset, double reso, double off_dac, double off_code)
{
    // 防越界保护
    if (row < 0 || row >= m_elTable->rowCount()) return;

    row = findCheckedValidIndex(mz, 1);
    // 更新 Gain (第2列) 和 Offset (第3列)
    m_elTable->setItem(row, 2, new QTableWidgetItem(QString::number(gain, 'f', 3)));
    m_elTable->setItem(row, 3, new QTableWidgetItem(QString::number(offset, 'f', 3)));

    // 更新 Reso (第5列，标红)
    QTableWidgetItem* itemReso = new QTableWidgetItem(QString::number(reso, 'f', 1));
    itemReso->setForeground(QBrush(QColor(220, 50, 50)));
    m_elTable->setItem(row, 5, itemReso);

    // 更新 DAC (第6列，标红)
    QTableWidgetItem* itemDAC = new QTableWidgetItem(QString::number(off_dac, 'f', 3));
    itemDAC->setForeground(QBrush(QColor(220, 50, 50)));
    m_elTable->setItem(row, 6, itemDAC);

    // 更新 Code (第7列，标红)
    QTableWidgetItem* itemCode = new QTableWidgetItem(QString::number(off_code, 'f', 1));
    itemCode->setForeground(QBrush(QColor(220, 50, 50)));
    m_elTable->setItem(row, 7, itemCode);

    // 保留原有的生效勾选状态
    bool enabled = false;
    QTableWidgetItem* oldCheck = m_elTable->item(row, 9);
    if (oldCheck) enabled = (oldCheck->checkState() == Qt::Checked);
    m_elTable->setItem(row, 9, createCheckItem(enabled));

    // 重新居中对其
    m_elTable->item(row, 2)->setTextAlignment(Qt::AlignCenter);
    m_elTable->item(row, 3)->setTextAlignment(Qt::AlignCenter);
}
