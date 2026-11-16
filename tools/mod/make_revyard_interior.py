#!/usr/bin/env python3
"""Builds the second tuner-mod shop's interior: itnr_b.bnk, the retail USED-CAR
DEALER room (icdc_wld.bnk, "U.S. Cars (north)" / World classic) re-purposed as
a workshop. Reads the user's own game data, writes only into the mods overlay
(inside the enabled tuner-garage package):

  python -I tools/mod/make_revyard_interior.py --game C:\\TDU2-Gamedata\\TestDrive2 \\
         --userdata C:\\temp\\otdu2-win-userdata
  # -> <userdata>/mods/packages/tuner-garage/Euro/Bnk/interior/itnr_b.bnk

InteriorWalk resolves ETNR_<L> to itnr_<l>.bnk (ETNR_A keeps the tuning-shop
based itnr_a). A ETNR room remaps its car-seller desk (selectable type 1, the
dealer's hostess) to the tuner seller (type 23). The props (lifts, racks) are
laid on top at load time by the room's layout file - see docs/TUNER_MOD_PLAN.md.

Stage 1 (this file): the room itself, unmodified. Re-skin and layout follow.
"""
import argparse
import os
import shutil

GAME_BANK = os.path.join("euro", "bnk", "interior", "icdc_wld.bnk")


def build(game, userdata):
    src = os.path.join(game, GAME_BANK)
    if not os.path.isfile(src):
        raise SystemExit("no %s (check --game)" % src)
    out_dir = os.path.join(userdata, "mods", "packages", "tuner-garage", "Euro", "Bnk", "interior")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "itnr_b.bnk")
    shutil.copyfile(src, out_path)
    print("wrote %s (%.1f MB)" % (out_path, os.path.getsize(out_path) / 1e6))


def main():
    ap = argparse.ArgumentParser(description="Build the second tuner shop's interior (itnr_b.bnk).")
    ap.add_argument("--game", default=r"C:\TDU2-Gamedata\TestDrive2", help="the game root (read only)")
    ap.add_argument("--userdata", default=r"C:\temp\otdu2-win-userdata")
    args = ap.parse_args()
    build(args.game, args.userdata)


if __name__ == "__main__":
    main()
