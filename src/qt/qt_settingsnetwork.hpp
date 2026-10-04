#ifndef QT_SETTINGSNETWORK_HPP
#define QT_SETTINGSNETWORK_HPP

#include <QWidget>

#include "qt_settings_completer.hpp"

struct netcard_conf_t;

namespace Ui {
class SettingsNetwork;
}

class SettingsNetwork : public QWidget {
    Q_OBJECT

public:
    explicit SettingsNetwork(QWidget *parent = nullptr);
    ~SettingsNetwork();

    int  changed();

    void restore();
    void save(int soft);
    void setOffline(struct netcard_conf_t *confs, int machineId, int profile);

public slots:
    void onCurrentMachineChanged(int machineId);

private slots:
    void on_pushButtonConf1_clicked();
    void on_pushButtonConf2_clicked();
    void on_pushButtonConf3_clicked();
    void on_pushButtonConf4_clicked();
    void on_comboIndexChanged(int index);

    void enableElements(Ui::SettingsNetwork *ui);

private:
    Ui::SettingsNetwork *ui;
    int                  machineId = 0;
    struct netcard_conf_t *conf;   /* what the page shows and saves: net_cards_conf, or setOffline()'s */
    bool                 offline   = false;
    int                  offlineProfile = -1;

    int                  net_card_cfg_changed[4] = { 0, 0, 0, 0 };

    SettingsCompleter   *sc[4];
    SettingsCompleter   *scDevice[4];
};

#endif // QT_SETTINGSNETWORK_HPP
