#include "HCI.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    HCI w;
    w.show();
    return a.exec();
	
}
