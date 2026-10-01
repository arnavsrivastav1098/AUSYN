#pragma once

#include "../monitoring/system_snapshot.h"

#include <QWidget>

class QLabel;
class QTableWidget;

namespace Ausyn {

class DataQualityPage final : public QWidget {
    Q_OBJECT
public:
    explicit DataQualityPage(QWidget* parent = nullptr);
    void setSnapshot(const SystemSnapshot& snapshot);

private:
    QLabel* summary_ = nullptr;
    QTableWidget* table_ = nullptr;
};

} // namespace Ausyn
