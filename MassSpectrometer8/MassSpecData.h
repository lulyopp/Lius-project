#ifndef MASSSPECDATA_H
#define MASSSPECDATA_H

//定义全局数据结构
#include <QVector>
#include <QMetaType> // 元类型头文件

// 1. 仪器核心参数结构体
struct InstrumentParams {
    double position; // 位置参数
    double gain;     // 增益
    double offset;   // 整体偏移值
    double V;        // 局部微调偏移值
};

// 2. 单个波峰的分析结果结构体
struct PeakResult {
    bool found;      // 是否成功找到有效峰
    double targetMz; // 目标质量数
    double centerMz; // 实际出峰中心位置
    double height;   // 峰高
    double width;  // 峰宽
};

// 3. 趋势线（线性拟合）结果
struct TrendlineResult {
    double k;        // 斜率
    double b;        // 截距
    double rSquared; // R平方(判定系数)
};

// 4.自调参数设置
struct TuneSettings {
    //基础参数
    int maxIterations = 30;         // 最大迭代大循环次数 (默认 30)
    double rsdTarget = 20;          // Gain 阶段的 RSD 目标容差 (默认 10.0%)
    double peakWidthLower = 0.4;    // 峰宽下限阈值 (默认0.7）
    double peakWidthUpper = 0.9;    // 峰宽上限阈值 (默认1.4)
    double peakWidthRatio = 0.5;    // 自动调谐峰宽计算高度阈值
    double peakWidthRatio_1 = 0.1;  // 精确调谐峰宽阈值
    int scanCountPerEval = 1;       // 综合循环次数
    int averageMaxLength = 3;              // 采图平均数
    bool positionCrvEnabled = false; //是否强制执行
    double positionCrv_R2 = 0.999;//强制执行下的R值

    //高级参数
    double gainStep = 0.01;           // Gain 步长 (默认0.02)
    double offsetStep = 0.01;         // 偏移值 (Offset) 步长 (默认0.02)
    double minPeakHeight = 1.0e-14;   // 最小峰高阈值 (绝对强度)
    double peakSearchWindow = 5.0;    // 峰位最大偏移量（绝对）
    double stretchLower = 0.8;         // 形变率下限 (默认 0.8)
    double stretchUpper = 1.2;         // 形变率上限 (默认 1.2)
    double distancePenaltyWeight = 5.0; // 距离惩罚衰减权重 (默认 5.0)
    double fineTunePosStep1 = 1.0;
    double fineTunePosStep2 = 10.0;
    double fineTuneVStep1 = 0.001;
    double fineTuneVStep2 = 0.005;

    // === 新增：电参调整参数 ===
    double elecR2Target = 0.9999;       // 电参调整R²目标值
    double elecR2Min = 0.8;            // R²最低阈值，低于此直接失败
    int elecIconStep = 1;         // icon 调节步长
    int elecMcpStep = 2;          // mcp 调节步长
    int elecMaxCount = 3;              // 上下调节次数（如3次=7个值：0,+1,+2,+3,-1,-2,-3）
    double elecSmallMzThreshold = 18.0; // 小m/z阈值，小于此值先调offset
    double elecGainStep = 0.002;
    double elecOffsetStep = 0.002;
    int ep = 0;
    int em = 0;

};

// 5.特征峰
struct PeakCandidate {
    int index;
    double mz;
    double height;
};

// 调谐计算常量
namespace TuningConstants {
    constexpr double DacSlope = 6553.5;      // DAC code 转换斜率
    constexpr double DacOffset = 32767.5;    // DAC code 转换偏移
    constexpr double ResoFactor = 0.5;       // Reso 计算中 position * gain 的系数
    constexpr double FineTuneScanWindow = 3.0;     // 单一精调扫描半窗宽 (AMU)
    constexpr double FineTuneSinglePeakWindow = 0.45; // 单一精调寻峰窗口 (AMU)
    constexpr double FineTuneUnifiedMargin = 5.0;    // 统一精调扫描范围余量 (AMU)
    constexpr int FineTuneStabilizeMs = 3000;        // 精调等待稳定时间 (ms)
    constexpr int FineTuneStopCheckMs = 50;          // 精调急停检查间隔 (ms)
}

Q_DECLARE_METATYPE(InstrumentParams)
Q_DECLARE_METATYPE(TuneSettings)

#endif // MASSSPECDATA_H
