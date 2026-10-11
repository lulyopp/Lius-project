#include "AutoTuningWorker.h"

AutoTuningWorker::AutoTuningWorker(HardwareInterface* hw, QObject* parent)
    : QObject(parent) {

    hardInterface = hw; //rzy
}

// 自动调试模块
void AutoTuningWorker::doAutoTune(QList<double> targetMasses, QList<InstrumentParams> initialParams,
                                  TuneSettings settings, int tuneState) {
    m_forceStop.store(false);

    // 防卡死：并发重入拦截
    if (m_isScanning.exchange(true)) {
        emit logMessage("警告：仪器正在调谐中，请勿频繁点击！", true, 0);
        return;
    }

    if (targetMasses.isEmpty() || targetMasses.size() != initialParams.size()) {
        emit logMessage("错误：目标质量数为空或与初始参数不匹配！", true, 0);
        m_isScanning.store(false);
        emit workFinished();
        return;
    }

    QString tuneName = (tuneState == 0) ? "Gain" : (tuneState == 1 ? "偏移" : "Position");
    emit logMessage(QString(">>> 开始自动调节 %1...").arg(tuneName),false,0);

    //在每次gain调试开始前，初始化步长和初始方向
    double currentGainStep = settings.gainStep;
    int currentGainDir = 0;     // 0 代表初始未探索状态

    //在每次offset调试开始前，初始化步长和初始方向
    double currentOffsetStep = settings.offsetStep;
    int currentOffsetDir = 0;   // 0 代表初始未探索状态

    InstrumentParams params = initialParams[0];    //取第一行参数为基准
    QVector<double> currentPositions;
    for(const auto& p : initialParams)
    {
        currentPositions.append(p.position);
    }

    int iteration = 0;  //迭代次数

    try {
        while (iteration < settings.maxIterations)
        {
            iteration++;
            if (m_forceStop.load()) break;

            double off_dac = params.offset + params.V;
            double off_code = off_dac * TuningConstants::DacSlope + TuningConstants::DacOffset;

            // 更新参数表格
            for (int i = 0; i < targetMasses.size(); ++i)
            {
                double off_dac = params.offset + initialParams[i].V;
                double off_code = off_dac * TuningConstants::DacSlope + TuningConstants::DacOffset;
                double reso = TuningConstants::ResoFactor * currentPositions[i] * params.gain + off_code;
                emit tableUpdated(i, targetMasses[i], currentPositions[i], params.gain, params.offset, initialParams[i].V, reso, off_dac, off_code);
            }
            double baseReso = TuningConstants::ResoFactor * currentPositions[0] * params.gain + off_code;
            emit logMessage(QString("--- 正在下发第 %1 轮硬件参数并等待稳定 ---").arg(iteration),false,0);
            params.position = currentPositions[0];
            // 触发参数下发信号，但参数更新通过读表格下发。倒数第2位是 1：代表下发参数后命令硬件开始扫描；最后1为是1：代表下发参数后进行自动计算
            emit sChangeParam(params, baseReso, off_dac, off_code, 1, 0);

            if (m_forceStop.load()) break;

            // 事件循环阻塞等待 3 秒信号稳定
            for (int waitTime = 0; waitTime < 3000; waitTime += 100) {
                if (m_forceStop.load()) break; // 只要收到停止指令，瞬间跳出！
                QThread::msleep(100);
            }

            // ===============================================
            // 采图与目标评估循环
            // ===============================================
            QVector<PeakResult> lastValidResults;       //保存最新寻峰结果
            bool targetMetThisIteration = false;        //判断该次循环是否达标（迭代目标判断）
            bool peaksFoundAtLeastOnce = false;         //判断是否至少一次找齐峰
            int scanCount = settings.scanCountPerEval;                          //每次循环的扫描取图次数

            //多次扫描取图
            for (int tryCount = 1; tryCount <= scanCount; ++tryCount)
            {
                if (m_forceStop.load()) break;

                QVector<double> xData, yRaw;
                bool readSuccess = false;
                readSuccess = hardInterface->getSpectrumData(xData,yRaw,settings.averageMaxLength);

                //寻峰并判断本次扫描结果是否达标
                if (readSuccess)
                {
                    // 【P1修复】增加数据长度校验，防止空数据导致崩溃
                    if (xData.size() < 3 || yRaw.size() < 3) {
                        emit logMessage("警告：采集数据不足(<3个点)，跳过本轮评估！",true,0);
                        continue;
                    }
                    
                    bool allFoundThisTime = true;       //判断本次扫描的峰是否找齐
                    QVector<PeakResult> results = m_engine.analyzeMultiplePeaks(
                                xData, yRaw,
                                targetMasses,
                                settings.peakSearchWindow,  // 比如 5.0~10.0，允许的全局最大偏移量
                                settings.peakWidthRatio,    // 计算峰宽比例 (如 0.1)
                                settings.minPeakHeight,     // 最小基线噪声阈值 (如 1e-15)
                                settings.stretchLower,      // 形变率下限
                                settings.stretchUpper,      // 形变率上限
                                settings.distancePenaltyWeight  //距离惩罚权重系数
                                );
                    // 将整套多峰分析结果通过信号实时通知 UI
                    emit autoTunePeaksReady(results);
                    emit plotDataReady(xData, yRaw);
                    // 峰宽超限熔断警告
                    bool widthExceeded = false;
                    double badWidth = 0.0;
                    for(const auto& res:results)
                    {
                        if(res.found &&res.width > 5.0)
                        {
                            widthExceeded = true;
                            badWidth = res.width;
                            break;
                        }
                    }
                    if(widthExceeded)
                    {
                        emit logMessage(QString("<span style='color: #D13438;'><b> 严重错误：检测到异常峰宽Width ≈ %1 ！</span>").arg(badWidth, 0, 'f', 2),false,0);
                        emit logMessage("<span style='color: #0078D7;'>1. 调整<b>【峰宽计算阈值（%）】<b>(如从 10% 调至 50%）。</span>",false,0);
                        emit logMessage("<span style='color: #0078D7;'>2. 适当调整<b>【达标峰宽上下限】<b>。</span>",false,0);
                        emit logMessage("<span style='color: #D13438>;'><b> 强行终止自动调谐！</span>",true,0);
                        break;
                    }
                    for(const auto& r : results)
                    {
                        if (!r.found) {
                            allFoundThisTime = false;
                        }
                        emit logMessage(QString(" [波峰特征] m/z: %1 | 实际出峰: %2 | 峰宽：%3 ")
                                        .arg(r.targetMz, 4, 'f', 1).arg(r.centerMz, 6, 'f', 3).arg(r.width, 8, 'f', 5),false,0);
                    }

                    //若找齐峰，判断本次扫描结果是否达标
                    if (allFoundThisTime)
                    {
                        peaksFoundAtLeastOnce = true;
                        lastValidResults = results;
                        bool isGoalAchieved = false;        //判断本次扫描是否达到目标（扫描目标判断）

                        if (tuneState == 0)     //gain
                        {
                            QVector<double> w;
                            for (const auto& r : results)
                            {
                                w.append(r.width);
                            }
                            if(w.size()<2)
                            {
                                emit logMessage("警告：目标峰数量不足 2 个，无法评估 Gain 偏差，强制判定达标！",false,0);
                                isGoalAchieved = true;
                            }else
                            {
                                double rsd = m_engine.calculateRSD(w);
                                bool trendOk = (w.last() > w.first()); //判断不止一个目标峰
                                if (rsd < settings.rsdTarget && trendOk)
                                {
                                    isGoalAchieved = true;
                                }
                            }
                        }
                        else if (tuneState == 1)    //offset
                        {
                            isGoalAchieved = true;
                            for (const auto& r : results)
                            {
                                if (r.width < settings.peakWidthLower || r.width > settings.peakWidthUpper)
                                {
                                    isGoalAchieved = false;
                                }
                            }
                        }
                        else if (tuneState == 2)        //position
                        {
                            isGoalAchieved = true;  //position值的调试为只要找齐峰，就进入下一步拟合
                        }
                        if (isGoalAchieved)
                        {
                            if(tuneState != 2){
                                emit logMessage(QString("单循环第 %1 次采图评估：>>> [%2] 参数初判达标！<<<").arg(tryCount).arg(tuneName),false,0);
                                targetMetThisIteration = true;
                                break;  // 满足条件，提前跳出重试循环
                            }
                            else{
                                targetMetThisIteration = true;
                                break;  // 满足条件，提前跳出重试循环
                            }
                        }
                        else
                        {
                            emit logMessage(QString("第 %1 次采图评估：找齐峰位但目标未达标...").arg(tryCount),false,0);
                        }
                    }
                    else
                    {
                        emit logMessage(QString("第 %1 次采图评估：未找齐特征峰...").arg(tryCount),false,0);
                    }
                }
                QThread::msleep(800);
            }

            if (m_forceStop.load()) break;

            // ===============================================
            // 达标收尾 与 下一轮参数迭代
            // ===============================================
            //若本次迭代达到目标
            if (targetMetThisIteration)
            {
                if (tuneState == 2)
                {
                    // 【P0修复】防止 lastValidResults 数组越界
                    if (lastValidResults.size() != targetMasses.size()) {
                        emit logMessage(QString("错误：寻峰结果数量(%1)与目标数量(%2)不匹配，终止调谐！")
                                        .arg(lastValidResults.size()).arg(targetMasses.size()),true,0);
                        break;
                    }
                    
                    for (int i = 0; i < targetMasses.size(); ++i)
                    {
                        currentPositions[i] = m_engine.adjustPosition(currentPositions[i], lastValidResults[i].centerMz, targetMasses[i]);
                    }
                    if(targetMasses.size() >= 2)
                    {
                        TrendlineResult trend = m_engine.calculateTrendline(targetMasses.toVector(), currentPositions);
                        emit trendlineReady(trend.k, trend.b, trend.rSquared, targetMasses.toVector(), currentPositions);
                        
                        // 取消弹框确认，改为根据 positionCrvEnabled 自动判断
                        if (trend.rSquared < settings.positionCrv_R2 && !settings.positionCrvEnabled)
                        {
                            emit logMessage(QString("❌ R² = %1 < %2，未开启强制执行，终止 Position 调节和自动调谐！")
                                            .arg(trend.rSquared, 0, 'f', 6).arg(settings.positionCrv_R2, 0, 'f', 3), true, 0);
                            break;  // 退出整个调谐循环
                        }

                        emit logMessage(QString("✅ 使用趋势线公式计算所有 Position (R²=%1)")
                                        .arg(trend.rSquared, 0, 'f', 6), false, 0);
                        for (int i = 0; i < targetMasses.size(); ++i)
                        {
                            currentPositions[i] = trend.k * targetMasses[i] + trend.b;
                        }
                    }
                    else
                    {
                        emit logMessage("提示：目标峰只有 1 个，无需进行线性拟合，直接使用单点修正值。",false,0);
                    }
                }
                //无论调哪个参数，一旦确定达标，将最新的参数写回表格和硬件
                double final_off_dac = params.offset + params.V;
                double final_off_code = final_off_dac * TuningConstants::DacSlope + TuningConstants::DacOffset;

                // 【P0修复】防止 initialParams 数组越界
                if (initialParams.isEmpty() || initialParams.size() != targetMasses.size()) {
                    emit logMessage(QString("错误：初始参数数量(%1)与目标数量(%2)不匹配，终止调谐！")
                                    .arg(initialParams.size()).arg(targetMasses.size()),true,0);
                    break;
                }

                // 更新参数表格
                for (int i = 0; i < targetMasses.size(); ++i)
                {
                    double tempDac = params.offset + initialParams[i].V;  // 【修复】使用 initialParams[i].V 而非 initialParams[0].V
                    double tempCode = tempDac * TuningConstants::DacSlope + TuningConstants::DacOffset;
                    double final_reso = TuningConstants::ResoFactor * currentPositions[i] * params.gain + tempCode;
                    emit tableUpdated(i, targetMasses[i], currentPositions[i], params.gain, params.offset, initialParams[i].V,
                                      final_reso, tempDac, tempCode);
                }
                
                // 【新增】自动备份当前参数，防止闪退丢失
                QList<InstrumentParams> currentParams;
                for (int i = 0; i < targetMasses.size(); ++i) {
                    InstrumentParams p;
                    p.position = currentPositions[i];
                    p.gain = params.gain;
                    p.offset = params.offset;
                    p.V = initialParams[i].V;
                    currentParams.append(p);
                }
                emit autoBackupParams(targetMasses, currentParams, tuneState);
                
                double final_baseReso = TuningConstants::ResoFactor * currentPositions[0] * params.gain + final_off_code;
                emit logMessage(QString("+++ [%1] 调试完成，进行最终扫描... +++").arg(tuneName),false,0);
                params.position = currentPositions[0];
                //每个参数调参完成后再取一次图
                emit sChangeParam(params, final_baseReso, final_off_dac, final_off_code, 1, 0);
                //进行最后一次采图并分析绘制
                if (!m_forceStop.load())
                {
                    QVector<double> finalX, finalY;
                    if (hardInterface->getSpectrumData(finalX, finalY,settings.averageMaxLength))
                    {
                        // 【P1修复】最终验证时也增加数据长度校验
                        if (finalX.size() < 3 || finalY.size() < 3) {
                            emit logMessage("⚠️ 警告：最终验证数据不足，跳过分析！",false,0);
                        } else {
                            QVector<PeakResult> finalresults = m_engine.analyzeMultiplePeaks(
                                        finalX, finalY,
                                        targetMasses,
                                        settings.peakSearchWindow,  // 比如 5.0~10.0，允许的全局最大偏移量
                                        settings.peakWidthRatio,    // 计算峰宽比例 (如 0.1)
                                        settings.minPeakHeight,     // 最小基线噪声阈值 (如 1e-15)
                                        settings.stretchLower,      // 形变率下限
                                        settings.stretchUpper,      // 形变率上限
                                        settings.distancePenaltyWeight  //距离惩罚权重系数
                                        );
                            // 将整套多峰分析结果通过信号实时通知 UI
                            emit autoTunePeaksReady(finalresults);
                            // 发送信号让 UI 画出最后一张完美的图谱
                            emit plotDataReady(finalX, finalY);
                            emit logMessage("✅ 最终验证图谱已获取并绘制完毕！",false,0);
                            for(const auto& r : finalresults)
                            {
                                emit logMessage(QString(" 最终[波峰特征] m/z: %1 | 实际出峰: %2 | 峰宽：%3 ")
                                                .arg(r.targetMz, 4, 'f', 1).arg(r.centerMz, 6, 'f', 3).arg(r.width, 8, 'f', 5),false,0);
                            }
                        }
                    } else
                    {
                        emit logMessage("⚠️ 警告：最终验证扫描获取数据失败！",false,0);
                    }
                }

                emit logMessage(QString("+++ [%1] 调试结束！ +++").arg(tuneName),false,0);
                //最后一位是 0：静默下发！硬件更新寄存器后直接待机,不触发扫描
                emit sChangeParam(params, final_baseReso, final_off_dac, final_off_code, 0, 0);
                break;  //任务完成，跳出大循环
            }
            //若本次迭代结果未达到目标，进行迭代参数计算
            else
            {
                if (!peaksFoundAtLeastOnce)
                {
                    emit logMessage(QString("错误：连续 %1 次采图均未找齐特征峰！").arg(scanCount),false,0);
                    break;
                }
                emit logMessage("当前参数未达标，正在计算新参数进行下一次迭代...",false,0);
                bool dummyDone;
                if (tuneState == 0)
                {
                    params.gain = m_engine.adjustGain(lastValidResults,
                                                      params.gain,
                                                      currentGainStep,
                                                      currentGainDir,
                                                      settings.rsdTarget,
                                                      dummyDone);
                    emit logMessage(QString("当前Gain值的调节步长：%1").arg(currentGainStep),false,0);
                }
                else if (tuneState == 1)
                {
                    params.offset = m_engine.adjustOffset(lastValidResults,
                                                          params.offset,
                                                          currentOffsetStep,
                                                          currentOffsetDir,
                                                          settings.peakWidthLower,
                                                          settings.peakWidthUpper,
                                                          dummyDone);
                    emit logMessage(QString("当前偏移值的调节步长：%1").arg(currentOffsetStep),false,0);
                }
            }
            if (m_forceStop.load()) break;
        }
    }
    catch(const std::exception &ex)
    {
        emit logMessage(QString("致命异常：") + ex.what(),true,0);
    } catch(...) {
        // 【P2修复】捕获所有未知异常，防止闪退
        emit logMessage("未知致命异常！正在安全退出调谐流程...",true,0);
    }
    m_isScanning.store(false);
    emit logMessage(m_forceStop.load() ? ">>> 用户强制终止了调谐流程！" : ">>> 自动调谐流程全部结束！",true,0);
    emit workFinished();
}

//--------------------------------------精确调试模块
void AutoTuningWorker::doFineTuneScan(double targetMz, InstrumentParams params, TuneSettings settings) {
    m_forceStop.store(false);

    // 防卡死：并发重入拦截
    // 使用 exchange 原子操作，如果当前已经在扫描中，直接丢弃本次多余的指令，防止排队死锁
    if (m_isScanning.exchange(true)) {
        emit logMessage("警告：仪器正在扫描中，请勿频繁点击！",false,0);
        return;
    }

    emit logMessage(QString("--- 执行精确调参 (目标 m/z: %1) ---").arg(targetMz),false,0);

    // 计算参数并下发
    double off_dac = params.offset + params.V;
    double off_code = off_dac * TuningConstants::DacSlope + TuningConstants::DacOffset;
    double reso = TuningConstants::ResoFactor * params.position * params.gain + off_code;

    emit sChangeParam(params, reso, off_dac, off_code, 1, 0);

    // 阻塞等待
    for (int waitTime = 0; waitTime < TuningConstants::FineTuneStabilizeMs; waitTime += TuningConstants::FineTuneStopCheckMs) {
        if (m_forceStop.load()) {
            emit logMessage("已打断精调等待！",false,0);
            break; // 只要收到停止指令，瞬间跳出 3 秒等待
        }
        QThread::msleep(TuningConstants::FineTuneStopCheckMs);
    }

    if (!m_forceStop.load())
    {
        QVector<double> xData, yRaw;
        bool readSuccess = false;
        readSuccess = hardInterface->getSpectrumData(xData, yRaw,settings.averageMaxLength);

        if (readSuccess) {
            //QVector<double> ySmooth = m_engine.smoothData(yRaw);
            PeakResult res = m_engine.analyzeSinglePeak(xData, yRaw, targetMz,
                                                          TuningConstants::FineTuneSinglePeakWindow, settings.peakWidthRatio_1, settings.minPeakHeight,
                                                        settings.stretchLower,
                                                        settings.stretchUpper,
                                                        settings.distancePenaltyWeight);
            emit fineTuneResultReady(res.found, res.centerMz, res.width, res.height, xData, yRaw);
        }
        else {
            // 如果硬件读取失败（如通讯超时），发空数据解锁 UI，防止界面卡死
            emit logMessage("警告：精调采图超时或失败！",false,0);
            emit fineTuneResultReady(false, targetMz, 0.0, 0.0, QVector<double>(), QVector<double>());
        }
    }else
    {
        // 被打断时，发送一个空数据信号
        emit fineTuneResultReady(false, targetMz, 0.0, 0.0, QVector<double>(), QVector<double>());
    }

    // 防卡死：执行完毕，解除锁定，允许下一次点击
    m_isScanning.store(false);
}

// ---------------------------连续精调模块
//循环取图直到点击[停止];独立信号fineTuneWhileResultReady上报,勿连到handleFineTuneResult
//(其末尾stopInstrument会把硬件abort置位,杀死本循环)
void AutoTuningWorker::doFineTuneScanWhile(double targetMz, InstrumentParams params, TuneSettings settings) {
    m_forceStop.store(false);
    m_whileStop.store(false);

    // 防卡死：与单次精调/自动调谐互斥
    if (m_isScanning.exchange(true)) {
        emit logMessage("警告：仪器正在扫描中，请勿频繁点击！",false,0);
        return;
    }

    emit logMessage(QString("--- 连续精调开始 (目标 m/z: %1)，点击[停止]结束 ---").arg(targetMz),false,0);

    // 计算参数并下发(与单次精调一致)
    double off_dac = params.offset + params.V;
    double off_code = off_dac * TuningConstants::DacSlope + TuningConstants::DacOffset;
    double reso = TuningConstants::ResoFactor * params.position * params.gain + off_code;
    emit sChangeParam(params, reso, off_dac, off_code, 1, 0);

    // worker自行复位abort并清旧帧队列,不依赖UI侧时序
    hardInterface->startInstrument();
    hardInterface->sendParameters();

    // 阻塞等待参数稳定(可被任一停止打断)
    for (int waitTime = 0; waitTime < TuningConstants::FineTuneStabilizeMs; waitTime += TuningConstants::FineTuneStopCheckMs) {
        if (m_forceStop.load() || m_whileStop.load()) {
            emit logMessage("已打断精调等待！",false,0);
            break;
        }
        QThread::msleep(TuningConstants::FineTuneStopCheckMs);
    }

    // 主循环:取图→寻峰→上报,直到点击停止
    int failCount = 0;
    while (!m_forceStop.load() && !m_whileStop.load()) {
        emit sChangeTable();
        emit sChangeParam(params, reso, off_dac, off_code, 1, 0);
        QVector<double> xData, yRaw;
        if (hardInterface->getSpectrumData(xData, yRaw, settings.averageMaxLength)) {
            failCount = 0;
            PeakResult res = m_engine.analyzeSinglePeak(xData, yRaw, targetMz,
                                                          TuningConstants::FineTuneSinglePeakWindow, settings.peakWidthRatio_1, settings.minPeakHeight,
                                                          settings.stretchLower,
                                                          settings.stretchUpper,
                                                          settings.distancePenaltyWeight);
            emit fineTuneWhileResultReady(res.found, res.centerMz, res.width, res.height, xData, yRaw);
        } else {
            // 区分"用户停止"与"偶发采图失败":停止则退出;失败复位abort后重试
            if (m_forceStop.load() || m_whileStop.load())
                break;
            hardInterface->startInstrument();
            if (++failCount >= 3) {
                failCount = 0;
                emit logMessage("警告：连续采图超时，正在重试...",false,0);
            }
        }
    }

    emit logMessage(">>> 连续精调已停止。",false,0);
    // 收尾:发空结果,供UI侧解锁可能的等待状态(沿用现有防空等机制)
    emit fineTuneWhileResultReady(false, targetMz, 0.0, 0.0, QVector<double>(), QVector<double>());
    m_isScanning.store(false);
}

// ---------------------------统一精调模块
void AutoTuningWorker::doFineTuneUnifiedScan(QList<double> targetMasses, QList<InstrumentParams> paramsList, TuneSettings settings) {
    m_forceStop.store(false);

    if (targetMasses.isEmpty() || paramsList.isEmpty()) {
        emit logMessage("错误：统一精调目标质量数或参数为空！", true, 0);
        return;
    }

    // 防卡死：并发重入拦截
    if (m_isScanning.exchange(true)) {
        emit logMessage("警告：仪器正在扫描中，请勿频繁点击！", false, 0);
        return;
    }

    emit logMessage(QString("--- 执行统一精调扫描 (目标数: %1) ---").arg(targetMasses.size()), false, 0);

    // 用第一行参数触发 sChangeParam，handleChangeParam 会下发全表
    InstrumentParams firstParams = paramsList[0];
    double off_dac = firstParams.offset + firstParams.V;
    double off_code = off_dac * TuningConstants::DacSlope + TuningConstants::DacOffset;
    double reso = TuningConstants::ResoFactor * firstParams.position * firstParams.gain + off_code;

    emit sChangeParam(firstParams, reso, off_dac, off_code, 1, 0);

    // 等待稳定
    for (int waitTime = 0; waitTime < TuningConstants::FineTuneStabilizeMs; waitTime += TuningConstants::FineTuneStopCheckMs) {
        if (m_forceStop.load()) {
            emit logMessage("已打断统一精调等待！", false, 0);
            break;
        }
        QThread::msleep(TuningConstants::FineTuneStopCheckMs);
    }

    if (!m_forceStop.load()) {
        QVector<double> xData, yRaw;
        if (hardInterface->getSpectrumData(xData, yRaw, settings.averageMaxLength)) {
            QVector<PeakResult> results = m_engine.analyzeMultiplePeaks(
                xData, yRaw, targetMasses,
                settings.peakSearchWindow,
                settings.peakWidthRatio_1,
                settings.minPeakHeight,
                settings.stretchLower,
                settings.stretchUpper,
                settings.distancePenaltyWeight);
            emit fineTuneUnifiedResultReady(results, xData, yRaw);
        } else {
            emit logMessage("警告：统一精调采图超时或失败！", false, 0);
            emit fineTuneUnifiedResultReady(QVector<PeakResult>(), QVector<double>(), QVector<double>());
        }
    } else {
        emit fineTuneUnifiedResultReady(QVector<PeakResult>(), QVector<double>(), QVector<double>());
    }

    m_isScanning.store(false);
}

// ---------------------------电参数-强度调谐模块
void AutoTuningWorker::doIntensityTargetTuning(QList<double> targetMasses, QList<double> destIntens,
                                               QList<InstrumentParams> advParams, TuneSettings settings, bool emEnable)
{
    m_forceStop.store(false);

    // 防卡死：并发重入拦截
    if (m_isScanning.exchange(true)) {
        emit logMessage("警告：仪器正在调谐中，请勿频繁点击！", true, 0);
        return;
    }

    emit logMessage("> 开始强度拓展调谐 (Adv) ...",false,0);

    // 特殊日志内存池，用于存储 X 轴漂移警告
    QStringList specialWarningLogs;

    // 获取当前完整峰结果
    auto getCurrentPeaks = [&]() -> QVector<PeakResult>
    {
        QVector<double> xData, yRaw;
        if (hardInterface->getSpectrumData(xData, yRaw,settings.averageMaxLength))
        {
            QVector<PeakResult> results = m_engine.analyzeMultiplePeaks(
                xData, yRaw, targetMasses, settings.peakSearchWindow, 0.1,
                settings.minPeakHeight, settings.stretchLower, settings.stretchUpper, settings.distancePenaltyWeight);
            emit autoTunePeaksReady(results);
            emit plotDataReady(xData, yRaw);
            return results;
        }
        return QVector<PeakResult>();
    };
    //获取峰强度
    auto getCurrentIntensities = [&](const QVector<PeakResult>& peaks) -> QVector<double>
    {
        QVector<double> intens(targetMasses.size(), 0.0);
        for(int i = 0; i < peaks.size() && i < targetMasses.size(); ++i) {
            if(peaks[i].found) intens[i] = peaks[i].height;
        }
        return intens;
    };
    // 【新增】辅助函数：格式化所有峰的目标/当前强度对比字符串
    auto formatIntensCompare = [&](const QVector<double>& currents, const QVector<PeakResult> results) -> QString {
        QStringList parts;

        if(results.count() <= 0){
            return QString("无有效峰");
        }
        for(int idx = 0; idx < currents.size() && idx < destIntens.size() && idx < targetMasses.size(); ++idx) {
            parts.append(QString("m/z %1:目标=%2/当前=%3,峰宽=%4")
                         .arg(targetMasses[idx])
                         .arg(QString::number(destIntens[idx], 'e', 4))
                         .arg(QString::number(currents[idx], 'e', 4))
                         .arg(results[idx].width, 0, 'f', 2));
        }
        return parts.isEmpty() ? QString("无有效峰") : parts.join(", ");
    };

    // 初始化取图
    //emit sElecParams(settings.ep, settings.em, 0);
    emit sChangeAdvParam(advParams[0], 1);
    for (int waitTime = 0; waitTime < 1000; waitTime += 50) {
        if (m_forceStop.load()) {
            break; // 只要收到停止指令，瞬间跳出 1 秒等待
        }
        QThread::msleep(50);
    }// 等待稳定
    QVector<PeakResult> initialPeaks = getCurrentPeaks();
    QVector<double> currentIntens = getCurrentIntensities(initialPeaks);

    // 判定整体偏置还是非整体偏置
    int biasDir = m_engine.checkGlobalBiasDirection(currentIntens, destIntens.toVector());

    if(!m_forceStop.load())
    {
        // 整体偏置
        if (biasDir != 0)
        {
            emit logMessage(QString("--- 检测到整体偏置 (方向: %1) ---").arg(biasDir > 0 ? "偏高" : "偏低"),false,0);

            bool success = false;
            double bestR2 = -1.0;
            int bestIcon = settings.ep;

            emit logMessage("> 步骤1：开始调整离子源 Icon (ep) ...",false,0);
            // 循环次数: 如 maxCount=3，则 0, +1, +2, +3, -1, -2, -3
            QList<int> multipliers;
            multipliers.append(0);
            for(int i=1; i<=settings.elecMaxCount; ++i) { multipliers.append(i); multipliers.append(-i); }

            for (int m : multipliers)
            {
                if (m_forceStop.load()) break;

                int testEp = settings.ep + m * settings.elecIconStep;
                // 改变电参
                emit sElecParams(testEp, settings.em, 1);
                for (int waitTime = 0; waitTime < 1000; waitTime += 50)
                {
                    if (m_forceStop.load())
                    {
                        break; // 只要收到停止指令，瞬间跳出 3 秒等待
                    }
                    QThread::msleep(50);
                }// 等待电参稳定

                QVector<PeakResult> peaks = getCurrentPeaks();
                currentIntens = getCurrentIntensities(peaks);
                double r2 = m_engine.calculateGlobalIntensityR2(currentIntens, destIntens.toVector());
                emit logMessage(QString("<span style='color:blue;'>>> 探测 ep=%1, %2,  整体R²=%3</span>")
                                .arg(testEp)
                                .arg(formatIntensCompare(currentIntens, peaks))
                                .arg(r2, 0, 'f', 4),false,0);


                if (r2 > bestR2) {
                    bestR2 = r2;
                    bestIcon = testEp;
                }

                if (r2 >= settings.elecR2Target) {
                    emit logMessage("✅ Icon(ep)调整R2达标！调谐成功结束。",false,0);
                    success = true;
                    break;
                }
            }

            if (!success)
            {
                if (bestR2 < settings.elecR2Min) {
                    emit logMessage("<span style='color:red;'>> 步骤1失败：均低于最低阈值。正在将电参回调为初始值！</span>",false,0);
                    emit sElecParams(settings.ep, settings.em, 0);  //调谐失败，则调回初始ep,em
                    for (int waitTime = 0; waitTime < 1000; waitTime += 50)
                    {
                        if (m_forceStop.load())
                        {
                            break; // 只要收到停止指令，瞬间跳出 3 秒等待
                        }
                        QThread::msleep(50);
                    }
                    goto END_TUNE;
                }
                else
                {
                    emit logMessage(QString("> 步骤1选优：采用最佳 ep=%1 (R²=%2)，进入步骤2...").arg(bestIcon).arg(bestR2),false,0);
                    if(emEnable){
                        emit logMessage("> 步骤2：开始调整检测器 Mcp (em) ...",false,0);
                        // 单向探测
                        int dir = (biasDir > 0) ? -1 : 1;
                        int bestMcp = settings.em;
                        bool mcpSuccess = false;

                        for(int i=1; i <= settings.elecMaxCount * 2 + 1; ++i) { // 走满7次
                            if (m_forceStop.load()) break;
                            int testEm = settings.em + i * dir * settings.elecMcpStep;
                            // 改变电参
                            emit sElecParams(bestIcon, testEm, 1);
                            for (int waitTime = 0; waitTime < 1000; waitTime += 50) {
                                if (m_forceStop.load()) {
                                    break; // 只要收到停止指令，瞬间跳出 1 秒等待
                                }
                                QThread::msleep(50);
                            }// 等待电参稳定

                            QVector<PeakResult> peaks = getCurrentPeaks();
                            currentIntens = getCurrentIntensities(peaks);
                            double r2 = m_engine.calculateGlobalIntensityR2(currentIntens, destIntens.toVector());
                            emit logMessage(QString("<span style='color:blue;'>>> 单向探测 em=%1, %2, 整体R²=%3</span>")
                                            .arg(testEm)
                                            .arg(formatIntensCompare(currentIntens, peaks))
                                            .arg(r2, 0, 'f', 4),false,0);

                            if (r2 > bestR2) {
                                bestR2 = r2;
                                bestMcp = testEm;
                            }

                            if (r2 >= settings.elecR2Target) {
                                emit logMessage("✅ Mcp(em)调整R2达标！",false,0);
                                mcpSuccess = true;
                                break;
                            }
                        }

                        if(!mcpSuccess) {
                            if (bestR2 < settings.elecR2Min) {
                                emit logMessage("<span style='color:red;'>> 步骤2失败：所有探测的峰高差异均低于最低阈值。终止调谐！</span>",true,0);
                                emit sElecParams(settings.ep, settings.em, 0);  //调谐失败，则调回初始ep,em
                                for (int waitTime = 0; waitTime < 1000; waitTime += 50)
                                {
                                    if (m_forceStop.load())
                                    {
                                        break; // 只要收到停止指令，瞬间跳出 3 秒等待
                                    }
                                    QThread::msleep(50);
                                }
                                goto END_TUNE;
                            }
                            else {
                                emit logMessage(QString("> 步骤2选优结束，保留最佳em=%1(R²=%2)").arg(bestMcp).arg(bestR2),false,0);
                            }
                        }
                        emit logMessage(QString(">>> 整体偏置调谐结束，正在将最终最优电参写入硬件: ep=%1, em=%2, R2=%3").arg(bestIcon).arg(bestMcp).arg(bestR2),true,0);
                        emit sElecParams(bestIcon, bestMcp, 1);
                        for (int waitTime = 0; waitTime < 1000; waitTime += 50) {
                            if (m_forceStop.load()) {
                                break; // 只要收到停止指令，瞬间跳出 1 秒等待
                            }
                            QThread::msleep(50);
                        }// 等待电参稳定
                        QVector<double> final_xData, final_yRaw;
                        if (hardInterface->getSpectrumData(final_xData, final_yRaw,settings.averageMaxLength)) {
                            QVector<PeakResult> results = m_engine.analyzeMultiplePeaks(
                                        final_xData, final_yRaw, targetMasses, settings.peakSearchWindow, settings.peakWidthRatio,
                                        settings.minPeakHeight, settings.stretchLower, settings.stretchUpper, settings.distancePenaltyWeight);
                            emit autoTunePeaksReady(results);
                            emit plotDataReady(final_xData, final_yRaw);    //最终结果验证
                        }
                    }
                    else{
                        emit logMessage("⚠️ EM未打开，不进行EM调试！", false, 0);
                        int bestMcp = settings.em;
                        emit logMessage(QString(">>> 整体偏置调谐结束，正在将最终最优电参写入硬件: ep=%1, em=%2, R2=%3")
                                        .arg(bestIcon).arg(bestMcp).arg(bestR2),true,0);
                        emit sElecParams(bestIcon, bestMcp, 1);
                        for (int waitTime = 0; waitTime < 1000; waitTime += 50) {
                            if (m_forceStop.load()) {
                                break; // 只要收到停止指令，瞬间跳出 1 秒等待
                            }
                            QThread::msleep(50);
                        }// 等待电参稳定
                        QVector<double> final_xData, final_yRaw;
                        if (hardInterface->getSpectrumData(final_xData, final_yRaw,settings.averageMaxLength)) {
                            QVector<PeakResult> results = m_engine.analyzeMultiplePeaks(
                                        final_xData, final_yRaw, targetMasses, settings.peakSearchWindow, settings.peakWidthRatio,
                                        settings.minPeakHeight, settings.stretchLower, settings.stretchUpper, settings.distancePenaltyWeight);
                            emit autoTunePeaksReady(results);
                            emit plotDataReady(final_xData, final_yRaw);    //最终结果验证
                        }
                    }
                }
            }
            else{
                emit logMessage(QString("<span style='color:green;'>>>> 整体偏置调谐结束，正在将最终最优电参写入硬件: ep=%1, em=%2</span>")
                                .arg(bestIcon).arg(settings.em),false,0);
                emit sElecParams(bestIcon, settings.em, 1);
                for (int waitTime = 0; waitTime < 1000; waitTime += 50) {
                    if (m_forceStop.load()) {
                        break; // 只要收到停止指令，瞬间跳出 1 秒等待
                    }
                    QThread::msleep(50);
                }// 等待电参稳定
                QVector<double> final_xData, final_yRaw;
                if (hardInterface->getSpectrumData(final_xData, final_yRaw,settings.averageMaxLength)) {
                    QVector<PeakResult> results = m_engine.analyzeMultiplePeaks(
                                final_xData, final_yRaw, targetMasses, settings.peakSearchWindow, settings.peakWidthRatio,
                                settings.minPeakHeight, settings.stretchLower, settings.stretchUpper, settings.distancePenaltyWeight);
                    emit autoTunePeaksReady(results);
                    emit plotDataReady(final_xData, final_yRaw);    //最终结果验证
                }
            }
        }
        // 非整体偏置
        else
        {
            emit logMessage("--- 检测到非整体偏置，开始逐个峰调节 ---",false,0);

            for (int i = 0; i < targetMasses.size(); ++i)
            {
                if (m_forceStop.load()) break;

                double mz = targetMasses[i];
                double dest = destIntens[i];
                double currentR2 = m_engine.calculateSingleIntensityR2(currentIntens[i], dest);

                if (currentR2 >= settings.elecR2Target) {
                    emit logMessage(QString("<span style='color:green;'>> m/z %1 强度已达标 (R²=%2, 当前强度:%3, 目标强度:%4, 峰宽：%5)，跳过。</span>")
                                    .arg(mz).arg(currentR2, 0, 'f', 4).arg(QString::number(currentIntens[i], 'e', 4))
                                    .arg(QString::number(dest, 'e', 4)).arg(initialPeaks[i].width, 0, 'f', 2),false,0);
                    continue;
                }

                bool isSmallMz = (mz <= settings.elecSmallMzThreshold);  //判断小mz
                emit logMessage(QString("> 开始调节 m/z %1 (%2质量数)").arg(mz).arg(isSmallMz ? "小" : "大"),false,0);

                // 连坐机制标识
                bool localSuccess = false;
                double bestPeakR2 = currentR2; // 记录该峰在所有尝试中的最高得分

                // -----------------------------------------------------------------
                // 辅助函数：用于当前峰局部调节 Offset (type=1) 或 Gain (type=2)
                auto tuneLocalParam = [&](int tuneType) -> int {
                    QString paramName = (tuneType == 1) ? "Offset" : "Gain";
                    double originalVal = (tuneType == 1) ? advParams[i].offset : advParams[i].gain;
                    double stepSize = (tuneType == 1) ? settings.elecOffsetStep : settings.elecGainStep;

                    double bestVal = originalVal;
                    double localBestR2 = -1.0;
                    int status = 0;     //2完美达标，1选优达标，0未达标

                    int searchDir = (dest > currentIntens[i]) ? -1 : 1;

                    //步进调整参数
                    for(int m = 0; m <= 2 * settings.elecMaxCount + 1; ++m) {
                        if (m_forceStop.load()) break;

                        double testVal = originalVal + m * searchDir * stepSize;
                        if (tuneType == 1) advParams[i].offset = testVal;
                        else advParams[i].gain = testVal;

                        // 1. 重新计算当前行的衍生控制参数
                        double off_dac = advParams[i].offset + advParams[i].V;
                        double off_code = off_dac * TuningConstants::DacSlope + TuningConstants::DacOffset;
                        double reso = TuningConstants::ResoFactor * advParams[i].position * advParams[i].gain + off_code;

                        // 2. 发送信号：通知主线程刷新 Adv 表格 UI
                        emit advTableUpdated(i, mz, advParams[i].gain, advParams[i].offset, reso, off_dac, off_code);

                        // ⚠️ 3. 将新参数下发给硬件，并触发扫图
                        emit sChangeAdvParam(advParams[i], 1);
                        // 事件循环阻塞等待 3 秒信号稳定
                        for (int waitTime = 0; waitTime < 3000; waitTime += 100) {
                            if (m_forceStop.load()) break; // 只要收到停止指令，瞬间跳出！
                            QThread::msleep(100);
                        }

                        // 4. 取图并计算靶向 R² 得分
                        QVector<PeakResult> testPeaks = getCurrentPeaks();
                        QVector<double> intens = getCurrentIntensities(testPeaks);
                        double r2 = m_engine.calculateSingleIntensityR2(intens[i], dest);

                        emit logMessage(QString("<span style='color:blue;'>>> 探测 m/z %1: %2=%3, 当前单峰R²=%4, 目标%5/当前%6, 峰宽=%7</span>")
                                        .arg(mz).arg(paramName).arg(testVal).arg(r2, 0, 'f', 4)
                                        .arg(QString::number(dest, 'e', 4)).arg(QString::number(intens[i], 'e', 4)).arg(testPeaks[i].width, 0, 'f', 2),false,0);

                        // 选优记录
                        if (r2 > localBestR2) {
                            localBestR2 = r2;
                            bestVal = testVal;
                        }
                        if(r2 > bestPeakR2){
                            bestPeakR2 = r2;
                        }

                        // 如果达标，直接结束该参数的调节
                        if (r2 >= settings.elecR2Target) {
                            emit logMessage(QString("✅ m/z %1 的 %2:%3 调整达标！R2:%4").arg(mz).arg(paramName).arg(bestVal).arg(r2, 0,'f', 4),false,0);
                            status = 2;
                            break;
                        }
                    }
                    // 如果该参数的所有盲探均未达标（例如只达到了 lowestR2），必须让硬件回退到最优值
                    if (status != 2 && !m_forceStop.load()) {
                        if (tuneType == 1) advParams[i].offset = bestVal;
                        else advParams[i].gain = bestVal;

                        double off_dac = advParams[i].offset + advParams[i].V;
                        double off_code = off_dac * TuningConstants::DacSlope + TuningConstants::DacOffset;
                        double reso = TuningConstants::ResoFactor * advParams[i].position * advParams[i].gain + off_code;

                        emit advTableUpdated(i, mz, advParams[i].gain, advParams[i].offset, reso, off_dac, off_code);
                        // ⚠️
                        emit sChangeAdvParam(advParams[i], 1 );
                        // 事件循环阻塞等待 3 秒信号稳定
                        for (int waitTime = 0; waitTime < 3000; waitTime += 100) {
                            if (m_forceStop.load()) break; // 只要收到停止指令，瞬间跳出！
                            QThread::msleep(100);
                        }

                        if(localBestR2 >= settings.elecR2Min)
                        {
                            status = 1;
                        }
                        else
                        {
                            status = 0;
                        }
                    }
                    return status;
                };
                // -----------------------------------------------------------------

                int step1Status = 0;
                int step2Status = -1;
                // 执行优先级判定
                if (isSmallMz) {
                    emit logMessage("...优先级：先调 Offset...",false,0);
                    step1Status = tuneLocalParam(1); // 1 代表调 Offset
                    if (step1Status == 1) {
                        emit logMessage("<span style='color:red;'> ...Offset 选优结束，继续调 Gain...</span>",false,0);
                        step2Status = tuneLocalParam(2);  // 2 代表调 Gain
                    }
                }
                else {
                    emit logMessage("...优先级：先调 Gain...",false,0);
                    step1Status = tuneLocalParam(2);
                    if (step1Status == 1) {
                        emit logMessage("<span style='color:red;'> ...Gain 选优结束，继续调 Offset...</span>",false,0);
                        step2Status = tuneLocalParam(1);
                    }
                }

                // 在当前 m/z 选优结束后，顺路检查全局 X 轴漂移
                // ---------------------------------------------------------
                if(!m_forceStop.load()){
                    QVector<PeakResult> finalPeaks = getCurrentPeaks(); // 获取最后硬件锁定状态的谱图
                    QVector<double> actualCenters, validTargets;

                    for(int k=0; k < finalPeaks.size(); ++k) {
                        if(finalPeaks[k].found) {
                            validTargets.append(targetMasses[k]);
                            actualCenters.append(finalPeaks[k].centerMz);
                        }
                    }

                    // 只有当至少识别到2个峰时，才能计算线性趋势
                    if (validTargets.size() >= 2) {
                        double xAxisR2 = m_engine.calculateTrendline(validTargets, actualCenters).rSquared;
                        // 若低于及格线，不报红不终止，仅存入特殊日志内存池
                        if (xAxisR2 < settings.elecR2Min) {
                            QString warnMsg = QString("<span style='color:#FF8C00;'>[特殊监控] 调节 m/z %1 后，全局 X轴偏移超限！当前质量轴线性 R² = %2 (阈值 %3)</span>")
                                    .arg(mz).arg(xAxisR2, 0, 'f', 4).arg(settings.elecR2Min);
                            specialWarningLogs.append(warnMsg);
                        }
                    }
                }
                // ---------------------------------------------------------

                // 综合评估本峰最终结果
                if(step1Status == 0)
                {
                    emit logMessage(QString("<span style='color:red;'>> 步骤1失败：m/z %1 参数调谐失败！</span>").arg(mz),false,0);
                    localSuccess = false;
                }
                else if(step1Status == 1 && step2Status == 0)
                {
                    emit logMessage(QString("<span style='color:red;'>> 步骤2失败：m/z %1 参数调谐失败！</span>").arg(mz),false,0);
                    localSuccess = false;
                }
                else
                {
                    localSuccess = true;
                    if(step1Status != 2 && step2Status != 2)
                    {
                        emit logMessage(QString("<span style='color:red;'>> m/z %1 选优通过，保留最佳R2(%2)！最优参数已更新在表格中！</span>").arg(mz).arg(bestPeakR2, 0, 'f', 4),false,0);
                    }
                }

                // 【连坐机制】: 只要有一个峰不通过，立刻终止整个大流程！
                if (!localSuccess) {
                    emit logMessage(QString("<span style='color:red;'>> 警告：m/z %1 无法达到最低阈值 (R²=%2)！强制终止非整体调谐！</span>")
                                    .arg(mz).arg(settings.elecR2Min),false,0);
                    break;
                }
            }
        }
    }

END_TUNE:
    m_isScanning.store(false);
    // 在终止前，集中爆出内存中的所有特殊日志
    // ========================================================
    if (!specialWarningLogs.isEmpty()) {
        emit logMessage("=============================================",false,0);
        emit logMessage("<span style='color:#FF8C00;'>[调试提示] 本次调谐出现以下 X 轴严重偏移事件：</span>",false,0);
        for (const QString& msg : specialWarningLogs) {
            emit logMessage(msg,false,0);
        }
        emit logMessage("=============================================",false,0);
    }

    emit logMessage("> 强度调谐流程执行完毕！",false,0);

    // 恢复UI状态（可以复用 workFinished）
    emit workFinished();
}

