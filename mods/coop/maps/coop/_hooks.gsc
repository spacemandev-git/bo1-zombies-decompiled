// COOP MOD - not part of the original game. With +set fs_mods "coop ..." (mods/_stack/maps/_modstack.gsc): every
// player gets the character the co-op lobby picked (bo1_lobby_chars), also duplicates and client numbers 4-7. The
// co-op launch adds it when needed (web/shared/launch.ts, tools/coop.ps1). maps\coop\_coop.gsc, mods/coop/README.md.

register()
{
	maps\_modstack::add( "_zombiemode_ffotd::main_end", maps\coop\_coop::main_end ); // end of maps\_zombiemode::main()
}
