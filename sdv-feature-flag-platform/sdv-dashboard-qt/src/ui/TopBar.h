#pragma once



#include <QFrame>

#include <QString>

#include <QVector>



class QLabel;

class QPushButton;



namespace sdv::dash::ui {



class TopBar : public QFrame {

    Q_OBJECT

public:

    explicit TopBar(QWidget* parent = nullptr);



signals:

    void viewModeChanged(const QString& mode);



private slots:

    void tickClock();



private:

    void selectMode(QPushButton* active);



    QPushButton* makeSegment(const QString& label, bool active);

    QLabel* clock_{nullptr};

    QVector<QPushButton*> modeButtons_;

};



}  // namespace sdv::dash::ui

