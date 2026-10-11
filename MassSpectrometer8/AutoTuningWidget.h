#ifndef AUTOTUNINGWIDGET_H
#define AUTOTUNINGWIDGET_H

#include <QWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QGroupBox>
#include <QThread>
#include <QMap>
#include <QMenu>
#include <QAction>
// 参数设置弹窗
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include "qcustomplot.h"
#include "HardwareInterface.h"
#include "AutoTuningWorker.h"

#include "FineTuneWgt.h"

#include <QVariantMap>

class Dialog_tune;
class sConfigure;

class AutoTuningWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AutoTuningWidget(HardwareInterface* hw, QWidget *parent = nullptr);
    ~AutoTuningWidget() override;
    void closeEvent(QCloseEvent *event) override;

    // 设置创建 Dialog_tune 所需的参数
    void setTuneDialogParams(const QString& title, sConfigure* configure, const QString& tuneFilePath);
    void setParams(int i_ep,int i_em);

public slots:
    // 打开校准对话框（只读显示）
    void showTuneDialog();

public:
    void setConfigFilePath(QString path);  //设置配置文件的路径，不包含文件名
private slots:
    //表格菜单
    void onTableContextMenu(const QPoint& pos);
    void onDeleteRow();
    void onInsertRow();
    void clearSpanForColumn(int col);
    void loadFromUI();
    void loadFromPropFile();
    void saveAsPropFile();
    void saveToPropFile(const QString& path);
    void autoCalParams();
    void allSelect();   //一键全选
    void allCancel();   //取消全选
    // 全局控制
    void onTuneGainClicked();       //gain
    void onTuneOffsetClicked();     //偏移
    void onTunePositionClicked();   //position
    // 统一下发指令的内部函数
    void startSpecificTuning(int tuneState);
    void onStopClicked();           //停止
    void onMousePress(QMouseEvent *event); // 鼠标移动事件处理
    void onGlobalPlotMousePress(QMouseEvent *event);
    // 设置弹窗
    void onSettingsClicked();
    //参数表格保存与载入
    void onSaveTableClicked();
    void onLoadTableClicked();
    // Adv 表格的载入与保存
    void onDeleteEleRow();
    void onInsertEleRow();
    void onEleTableContextMenu(const QPoint& pos);
    void onLoadAdvTableClicked();
    void onSaveAdvTableClicked();
    //基础参数与高级参数载入
    void onLoadParamsClicked();
    //日志保存
    void onSaveLogClicked();

    // m_paramTable 排序与同步
    void onParamTableItemChanged(QTableWidgetItem* item);
    void onSyncParamTableToDialog();

    // 精调控制
    void onFineTuneTargetChanged(int index);    //根据目标m/z改变，进行刷新
    void onFineTuneAdjustClicked();             //position和V值点击调整
    void onStartFineTuneClicked();              //开始精调的触发函数
    void onStopFineTuneClicked();               //精调停止
    void onStartFineTuneUnifiedScan(QList<double> targetMasses, QList<int> paramTableRows);  //统一调整开始精调
    void onStopFineTuneUnifiedScan();           //统一调整停止精调

    // 接收 Worker 回调
    // 接收日志信息
    void handleLogMessage(const QString &msg,const bool& lightShow = false,const int& level = 0);
    // 接收质谱图数据
    void handlePlotDataReady(const QVector<double> &x, const QVector<double> &y);
    // 接收更新表格全部参数的槽函数
    void handleTableUpdate(int rowIndex, double mz, double pos, double gain, double offset, double V, double reso, double off_dac, double off_code);
    // 接收拟合结果并绘图
    void handleTrendlineReady(double k, double b, double rSquared, QVector<double> x, QVector<double> y);
    // 接收精调面板参数
    void handleFineTuneResult(bool found, double centerMz, double width, double height, QVector<double> x, QVector<double> y);
    // 接收统一精调结果
    void handleFineTuneUnifiedResult(QVector<PeakResult> results, QVector<double> xData, QVector<double> yData);
    // 接收参数变化
    void handleChangeParam(InstrumentParams params, double reso, double offset_dac, double offset_code, bool startScan, bool isAutoCal);
    // 接收电参变化
    void handleElecParam(int ep, int em, bool isScan);
    // 接收adv参数变化
    void handleChangeAdvParam(InstrumentParams params, bool startScan);
    // adv表格刷新
    void handleAdvTableUpdate(int row, double mz, double gain, double offset, double reso, double off_dac, double off_code);
    //处理自动调谐周期内发回的多峰分析结果
    void handleAutoTunePeaksReady(QVector<PeakResult> results);
    void handleWorkFinished();

    //自动调谐校准参数表
    QVariantList getTableData(bool onlyEnabled = false);
    int getMaxMz();

    //自动调谐拓展 电参
    QVariantList getTableEleData(bool onlyEnabled = false);
    
    // 【新增】自动备份参数
    void handleAutoBackupParams(QList<double> targetMasses, QList<InstrumentParams> currentParams, int tuneState);
    
    // 【新增】从备份恢复参数
    void restoreFromBackup();

    //adv的按钮
    void onSyncParamClicked();
    void onStartExecClicked();
    void onForceStopClicked();

    void applyParams();

signals:
    void setParaRes(QVariantList res, bool isAuto);  //设置自动调谐结果到表格中
    void controlScan(bool isstrat);     //控制扫描
    void sSetScanRange(int startMz, int endMz);// 控制精调扫描范围 (起点，终点)
    void savePropFile();  //调用原本的保存prop配置文件接口，前提是已经把自动调谐的表格内容同步到原本的表格里面
    void showDialogTune(); //显示原本的dialog_tune
    void setElecPara(int ep, int em);

private:
    void setupUI();
    void setupThread();
    void initTableMenu();
    void initEleTableMenu();

    int findCheckedValidIndex(const double& mz,int i_tableType);

    HardwareInterface* m_hardware;
    QThread* m_workerThread = nullptr;
    AutoTuningWorker* m_worker = nullptr;

    //表格菜单
    QMenu *m_contextMenu = nullptr;       // 右键菜单
    QAction *m_actDelete = nullptr;       // 删除行
    QAction *m_actInsert = nullptr;       // 插入行
    //QAction *m_actLoadFromUI = nullptr;   //从软件其他的table获取
    QAction *m_actLoadPropFile = nullptr;   //从文件获取
    QAction *m_actAutoCal = nullptr;   //自动计算
    QAction *m_actSyncToDialog = nullptr;   //手动同步到Dialog_tune
    QAction *m_actAllSelect = nullptr;  //一键全选
    QAction *m_actAllCancel = nullptr;  //一键取消全选


    //adv参数表菜单
    QMenu *m_contextEleMenu = nullptr;       // 右键菜单
    QAction *m_actEleDelete = nullptr;       // 删除行
    QAction *m_actEleInsert = nullptr;       // 插入行

    // 全局 UI
    QTableWidget* m_paramTable;     //参数表格
    QTextEdit* m_logTextEdit;       //日志
    QPushButton* m_btnTuneGain;     //gain调试
    QPushButton* m_btnTuneOffset;   //偏移调试
    QPushButton* m_btnTunePosition; //position调试
    QPushButton* m_btnStopDebug;    //停止
    QCustomPlot* m_customPlot;      //全局谱图
    QCustomPlot* m_fitPlot;         //局部谱图
    // Qcustomplot内置条目
    QCPItemText* m_coordLabel;
    QCPItemTracer* m_tracer;

    // 精调 UI
    QCustomPlot* m_localPlot;
    QComboBox* m_cmbFineTuneMz;
    QLabel* m_lblCurrentPos;
    QLabel* m_lblCurrentV;
    QLabel* m_lblPeakPos;
    QLabel* m_lblPeakWidth;
    QLabel* m_lblPeakHeight;
    QPushButton* m_btnStartFineTune;
    QPushButton* m_btnStopFineTune;

    FineTuneWgt* m_fineTuneWgt = nullptr;

    // 谱图数据缓存
    QVector<double> m_cachedXData;
    QVector<double> m_cachedYData;
    // 刷新局部精调图表的显示
    void refreshLocalPlot();

    TuneSettings m_tuneSettings; // 保存当前的全局设置
    QPushButton* m_btnSaveTable;    //表格保存按钮
    QPushButton* m_btnLoadTable;    //表格载入按钮
    QPushButton* m_btnSaveLog;      //日志保存按钮
    QPushButton* m_btnLoadPropFile;    //从prop文件加载到表格
    QPushButton* m_btnSavePropFile;    //从表格存到prop文件
    // Adv 界面的载入保存按钮
    QPushButton* m_btnLoadAdvTable;
    QPushButton* m_btnSaveAdvTable;
    // 拟合曲线参数k、b
    double m_currentK = 0.0;
    double m_currentB = 0.0;
    double m_currentR = 0.0;

    //配置文件路径
    QString m_configFilePath = "";

    //缓存寻峰结果
    QMap<double, PeakResult> m_peakResultCache;

    //隐式存储position和V
    double m_hiddenPosition = 0.0;
    double m_hiddenV = 0.0;

    //拉杆指示器和基准记忆变量
    QSlider* m_sliderPos;
    QSlider* m_sliderV;
    double m_initialPosition = 0.0; //记录开始调节前的初始位置
    double m_initialV = 0.0;        //记录开始调节前的初始峰宽

    //加载默认值
    void loadDefaultSettings();

    // 表格勾选辅助函数
    static QTableWidgetItem* createCheckItem(bool checked = false);
    static bool isRowEnabled(QTableWidget* table, int row);
    void refreshFineTuneMzCombo();

    // 表格输入校验与读取
    bool readDoubleFromTable(QTableWidget* table, int row, int col, double& outValue, const QString& fieldName);

    // 独立管理自己的 Dialog_tune（只读显示）
    Dialog_tune* m_tuneDialog = nullptr;
    QString m_tuneDialogTitle;
    sConfigure* m_tuneConfigure = nullptr;
    QString m_tuneFilePath;

    //adv的 UI
    QTableWidget* m_elTable = nullptr;
    QPushButton* m_btnSyncParam = nullptr;
    QPushButton* m_btnStartExec = nullptr;
    QPushButton* m_btnForceStop = nullptr;

    //关键信息显示栏
    QLabel* m_lbInfo = nullptr;
};

#endif // AUTOTUNINGWIDGET_H
