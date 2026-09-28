/*
 * MegaPPBox   86Box stripped down to run untouched Merit Megatouch images.
 *
 *             Network configuration dialog (as PeepeeBox's).
 *
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "qt_networksettings.hpp"
#include "qt_settingsnetwork.hpp"

#include <cstddef>
#include <cstdint>

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/megatouch.h>
}

NetworkSettings::NetworkSettings(QWidget *parent)
    : QDialog(parent)
    , page(new SettingsNetwork(this))
{
    setWindowTitle(tr("Network"));

    const auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(page);
    layout->addWidget(buttons);

    /* SettingsNetwork::save() writes straight into net_cards_conf[], so it must
       only run when the user accepts.  The cards and what they are plugged
       into -- a switch for Mega-Link, NAT, a host network card -- are kept
       with the image, which the profile reads back at the next hard reset;
       "None" on every card takes the cabinet's network card out. */
    connect(this, &QDialog::accepted, this, [this]() {
        page->save(0);
        megatouch_network_to_image();
    });
}
