/*
 * MegaPPBox   86Box stripped down to run untouched Merit Megatouch images.
 *
 *             Network configuration dialog (as PeepeeBox's), and the modem's
 *             two actions: in or out, and its settings.
 *
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "qt_networksettings.hpp"
#include "qt_settingsnetwork.hpp"
#include "qt_deviceconfig.hpp"

#include <cstddef>
#include <cstdint>

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/machine.h>
#include <86box/char.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/version.h>
#include <86box/megatouch.h>
}

NetworkSettings::NetworkSettings(QWidget *parent)
    : NetworkSettings(parent, QString(), -1)
{
}

/* image and profile: the Machine Manager's pick.  The running image (or none
   given) is the cabinet's own: its cards, net_cards_conf[], built at the next
   hard reset.  Any other image's settings are read from and kept with it. */
NetworkSettings::NetworkSettings(QWidget *parent, const QString &image, int profile)
    : QDialog(parent)
    , page(new SettingsNetwork(this))
{
    setWindowTitle(tr("Network"));

    const QString running = QString::fromUtf8(megatouch_image());
    const bool    offline = !image.isEmpty() && (profile >= 0) &&
                         ((QFileInfo(image) != QFileInfo(running)) || (profile != megatouch_profile()));
    if (offline) {
        this->image   = image.toUtf8();
        this->profile = profile;
        confs         = new netcard_conf_t[NET_CARD_MAX]();
        megatouch_network_load(this->image.constData(), profile, confs);
        page->setOffline(confs, machine_get_machine_from_internal_name(mt_profiles[profile].machine), profile);
    }

    const auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(page);
    layout->addWidget(buttons);

    /* SettingsNetwork::save() writes straight into its cards (net_cards_conf[]
       for the running image), so it must only run when the user accepts.  The
       cards and what they are plugged into -- a switch for Mega-Link, NAT, a
       host network card -- are kept with the image, which the profile reads
       back at the next hard reset; "None" on every card takes the cabinet's
       network card out. */
    connect(this, &QDialog::accepted, this, [this, offline]() {
        page->save(0);
        if (offline)
            megatouch_network_store(this->image.constData(), this->profile, confs);
        else
            megatouch_network_to_image();
    });
}

NetworkSettings::~NetworkSettings()
{
    delete[] confs;
}

/* Tools > Modem on COM2: the modem goes in or out at once, and stays so with
   the image.  A release that looked for it at boot (Linux's setserial and
   pppd, the DOS game's own probe) only sees the change at its next boot, so
   a restart is offered -- not made. */
void
mt_plug_modem(QWidget *parent, bool on)
{
    const char *img = megatouch_image();

    if (!img || !img[0])
        return;
    megatouch_set_image_option(img, MT_OPT_MODEM, on);
    config_save();

    startblit();
    megatouch_modem_plug(on);
    endblit();

    QMessageBox box(QMessageBox::Question, EMU_NAME,
                    on ? QObject::tr("The modem is connected to COM2.")
                       : QObject::tr("The modem is disconnected from COM2."),
                    QMessageBox::Yes | QMessageBox::No, parent);
    box.setInformativeText(QObject::tr("If the cabinet has already finished booting, it may not notice "
                                       "until it restarts. Restart it now?"));
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() == QMessageBox::Yes) {
        config_changed = 2;
        pc_reset_hard();
    }
}

/* Modem settings: the telephone line, and the host it dials.  Kept for every
   image; the fitted modem takes them at once (a call in progress keeps its
   host, and drops if the line is now not connected). */
bool
mt_configure_modem(QWidget *parent)
{
    if (!DeviceConfig::ConfigureDevice(&char_modem_megatouch_com_device, 2, parent))
        return false;
    config_save();

    startblit();
    modem_megatouch_reconfigure();
    endblit();
    return true;
}
