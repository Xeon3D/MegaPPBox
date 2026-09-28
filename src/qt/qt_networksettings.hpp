/*
 * MegaPPBox   86Box stripped down to run untouched Merit Megatouch images.
 *
 *             Network configuration dialog (as PeepeeBox's): upstream's network
 *             card page, on its own.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#ifndef QT_NETWORKSETTINGS_HPP
#define QT_NETWORKSETTINGS_HPP

#include <QDialog>

class SettingsNetwork;

class NetworkSettings : public QDialog {
    Q_OBJECT

public:
    explicit NetworkSettings(QWidget *parent = nullptr);

private:
    SettingsNetwork *page;
};

#endif // QT_NETWORKSETTINGS_HPP
