# Security keys

Every Megatouch release checks for Merit's security key, a Dallas iButton
(a DS1991, or a DS1205 MultiKey on the early XL CDs). MegaPPBox emulates the
key, but it needs a dump of a real one. **This repository does not include
any key dumps.**

## Importing a key

In the Machine Manager, **Import key…** (or **Key > Import key...** in the
cabinet window) copies dumps into the cabinet folder's `keys` folder:

* a **keyflasher** dump of a DS1991, 264 bytes (`*_full_*`), or
* a DS1205 MultiKey dump, 192 bytes.

MegaPPBox works out which release a dump is for from the dump itself, and keeps it as
`<family>_full_<ROM ID>` (or `<family>_multikey_<part>`). The Machine Manager
then fits it automatically to every image of that release. Where two releases
share a key format (MAXX and XL 6000), the import asks which one it is for,
unless the file's name already says.

| Family | Releases |
|---|---|
| `XLR1` | MegaTouch XL (R1) |
| `XL5K` | MegaTouch XL Super 5000 |
| `XL6K` | MegaTouch XL 6000 |
| `XGOLDCD` | MegaTouch XL Gold |
| `XPLAT` | XL Platinum, Double Platinum |
| `XTIT` | XL Titanium, Titanium 2 |
| `M1` | MAXX |
| `M2K` | MAXX 2000, 2000 Plus |
| `MDIAMOND` | MAXX Diamond, Double Diamond |
| `MEMERALD` | MAXX Emerald, Emerald 2 |
| `MRUBY` / `MRUBY2` | MAXX Ruby / Ruby 2 |
| `MSAPPHIRE` | MAXX Sapphire, Sapphire 2 |
| `MJADE` | MAXX Jade, Jade 2 |
| `MCROWN` | MAXX Crown |

## Building keys in (local builds only)

A build can also carry dumps inside the executable. Put them in this folder
with a `keys.txt` listing them, one `<file>|<name shown>` per line, and
reconfigure. The release's first listed dump is the one the Machine Manager
picks. Everything here except this file is git-ignored, so dumps stay out of
the repository.
