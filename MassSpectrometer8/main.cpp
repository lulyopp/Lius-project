#include <QApplication>
#include "HardwareInterface.h"
#include "AutoTuningWidget.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    // 1. 实例化硬件驱动
    HardwareInterface* hw = new RGAHardwardDrive();

    // 2. 实例化自动调试界面 (传入接口指针)
    AutoTuningWidget* autoWidget = new AutoTuningWidget(hw);
    autoWidget->setWindowTitle("自动与精确调参");
    autoWidget->resize(800, 600);
    autoWidget->show();

    return a.exec();
}
