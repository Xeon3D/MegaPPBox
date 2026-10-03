/*
 * MegaPPBox   86Box stripped down to run untouched Merit Megatouch images.
 *
 *             Network configuration dialog (as PeepeeBox's): upstream's network
 *             card page, on its own.  And the modem's actions.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#ifndef QT_NETWORKSETTINGS_HPP
#define QT_NETWORKSETTINGS_HPP

#include <QByteArray>
#include <QDialog>
#include <QString>

struct netcard_conf_t;

class SettingsNetwork;

class NetworkSettings : public QDialog {
    Q_OBJECT

public:
    explicit NetworkSettings(QWidget *parent = nullptr);
    NetworkSettings(QWidget *parent, const QString &image, int profile);
    ~NetworkSettings();

private:
    SettingsNetwork *page;
    QByteArray       image;   /* another image than the running one: its path */
    int              profile = -1;
    netcard_conf_t  *confs = nullptr; /* ...its cards, NET_CARD_MAX of them */
};

/* The modem on COM2, in or out now (offering a restart), and its settings
   (applied at once; false when cancelled). */
void mt_plug_modem(QWidget *parent, bool on);
bool mt_configure_modem(QWidget *parent);

#endif // QT_NETWORKSETTINGS_HPP
