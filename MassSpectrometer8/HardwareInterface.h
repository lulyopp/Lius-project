// --- HardwareInterface.h ---
#ifndef HARDWAREINTERFACE_H
#define HARDWAREINTERFACE_H

#include <QVector>
#include <cmath>
#include <QFile>
#include <QDebug>
#include <QTextStream>
#include <QStringList>
#include <QQueue>
#include "MassSpecData.h"
#include <functional>
#include <QMutex>
#include <QThread>

// =========================================================
// 硬件接口纯虚基类 (抽象层)
// =========================================================
class HardwareInterface {
public:
    virtual ~HardwareInterface() = default;

    // 1. 仪器启停
    virtual bool startInstrument() = 0;
    virtual bool stopInstrument() = 0;

    // 2. 参数下发 (接收底层计算好的硬件代码)
    virtual bool sendParameters() = 0;

    // 3. 谱图数据获取 (阻塞等待扫描完成并返回数据)
    virtual bool getSpectrumData(QVector<double>& xData, QVector<double>& yData, int maxLength) = 0;

    // 设置阀门开关
    virtual void setValveEnabled(bool open) = 0;
};

class RGAHardwardDrive:public HardwareInterface {
//signals:
//    void startRGA();
//    void stopRGA();
public:
    RGAHardwardDrive()=default;
    ~RGAHardwardDrive()=default;

    QVector<double> xAxis;
    QQueue<QVector<double>> yAxis;
    int maxLength = 3;
    QMutex mutex;
    std::atomic<bool> m_isAborted{false}; // 新增一个终止标志

private:
    QVector<double> mXAxis;
    QVector<double> mYAxis;
public:
    // 1. 仪器启停
    bool startInstrument()
    {
//        emit startRGA();
        m_isAborted.store(false); // 重置标志
        return true;
    }
    bool stopInstrument()
    {
//        emit stopRGA();
        m_isAborted.store(true); // 标记终止，打断正在等待的循环
        return true;
    }

    // 2. 参数下发 (接收底层计算好的硬件代码)
    bool sendParameters()
    {
        //下发新参数时，必须清空历史脏数据!
        mutex.lock();
        yAxis.clear();
        xAxis.clear();
        mutex.unlock();
        return true;
    }

    // 3. 谱图数据获取 (阻塞等待扫描完成并返回数据)
    bool getSpectrumData(QVector<double>& xData, QVector<double>& yData, int averageMaxLength)
    {
        if (averageMaxLength != 0)
        {
            maxLength = averageMaxLength;
        }
        while(true){
            // 防卡死机制。如果用户按了停止，立刻退出！
            if (m_isAborted.load()) {
                qDebug() << "获取谱图被用户强制打断！";
                return false;
            }
            if(mutex.tryLock(25)){
                if(yAxis.size()>=maxLength) {
                    while (yAxis.size() > maxLength) {
                        yAxis.dequeue(); // 扔掉老旧数据
                    }
                    QVector<QVector<double>> ySet;
                    for(int i=1;i<=maxLength;i++){
                        ySet.push_back(yAxis.dequeue());
                    }

                    int firstSize = -1; //长度均一性检查
                    for(auto &s:ySet){
                        if(firstSize==-1){
                            firstSize = s.size();
                        }
                        if(firstSize != s.size()){
                            qDebug()<<"Error:size not queal!";
                            mutex.unlock();
                            //assert(0);//报错
                            return false;
                        }
                    }

                    QVector<double> yAverage(ySet[0].size(),0);
                    for(int i=0;i<ySet.size();i++){
                        for(int j=0;j<ySet[i].size();j++){
                            yAverage[j]+=ySet[i][j]/maxLength;
                        }
                    }
                    xData=xAxis;
                    yData=yAverage;
                    qDebug()<<"Read 3 vector";
                    //
                    qDebug().noquote() << "val0:    " << yAverage[0];
                    qDebug().noquote() << "val5:    " << yAverage[5];
                    qDebug().noquote() << "val10:    " << yAverage[10];
                    qDebug().noquote() << "val20:    " << yAverage[20];
                    int count = yAverage.count();
                    qDebug().noquote() << "val-count-2:    " << yAverage[count-2];
                    qDebug().noquote() << "val-count-1:    " << yAverage[count-1];
                    //
                    mutex.unlock(); //rzy
                    return true;
                }else{
                    mutex.unlock();
                    QThread::msleep(100);
                }
            }else{
                QThread::msleep(100);
            }
        }
    }

    void setValveEnabled(bool open){

    }
};

#endif // HARDWAREINTERFACE_H
