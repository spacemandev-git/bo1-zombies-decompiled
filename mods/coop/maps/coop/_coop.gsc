// COOP MOD - not part of the original game. Loaded only with +set fs_mods "coop ..." (maps/coop/_hooks.gsc); the base
// game never calls it. mods/coop/README.md says which parts are checked and which follow the expected retail structure.
//
// The co-op lobby picks a character (0-3) for every player and gives the first player of each character that client
// number (web/shared/launch.ts): with up to four different characters the retail scripts need nothing. A player who
// picked a character someone else has gets client number 4, 5, ...; the host's engine dvar bo1_lobby_chars lists the
// character of every client number ("0 1 2 3 0", "-" for an unused number). The retail maps give a player the
// character of their entity number, cases 0-3. This mod, run at the end of maps\_zombiemode::main()
// (_zombiemode_ffotd::main_end), wraps the level function pointers the retail maps set before calling
// maps\_zombiemode::main() (EXPECTED from the retail structure, NOT CHECKED here - the scripts are in the fastfiles;
// check them with the engine command bo1_dumpscript, README "Check it"):
//   level.zombiemode_give_player_model_override( entity_num )     the character's body
//   level.zombiemode_player_set_viewmodel_override( entity_num )  the character's arms
// so they run with the lobby's character instead of the entity number, and sets self.entity_num (the retail player index
// for the character's voice lines and per-character tables) to the character. Every decision goes to the game log
// (games_mp.log) as a "coop: ..." line.
//
// Only level function pointers and functions of this file are referenced: a call to a function of another file that
// does not exist on some map would stop the script from loading (the engine resolves them at load).
//
// Dvars (host): coop_mod 0/1 (default 1; 0 = this mod does nothing), coop_entity_num 0/1 (default 1; 0 = leave
// self.entity_num at the entity number, the models still follow the lobby).

main_end()
{
	if ( GetDvar( #"coop_mod" ) == "" )
	{
		SetDvar( "coop_mod", "1" );
	}
	if ( GetDvar( #"coop_entity_num" ) == "" )
	{
		SetDvar( "coop_entity_num", "1" );
	}
	chars = GetDvar( #"bo1_lobby_chars" );
	level.coop_chars = coop_parse_chars( chars );
	coop_log( "map " + GetDvar( #"mapname" ) + ", bo1_lobby_chars \"" + chars + "\", coop_mod " + GetDvarInt( #"coop_mod" ) + ", coop_entity_num " + GetDvarInt( #"coop_entity_num" ) );
	for ( i = 0; i < 32; i++ )
	{
		if ( IsDefined( level.coop_chars[i] ) && level.coop_chars[i] != i )
		{
			coop_log( "client " + i + " plays character " + level.coop_chars[i] );
		}
	}
	if ( GetDvarInt( #"coop_mod" ) == 0 )
	{
		coop_log( "off (coop_mod 0): retail characters" );
		return;
	}

	if ( IsDefined( level.zombiemode_give_player_model_override ) )
	{
		level.coop_give_player_model_orig = level.zombiemode_give_player_model_override;
		level.zombiemode_give_player_model_override = ::coop_give_player_model;
		coop_log( "body: wrapped level.zombiemode_give_player_model_override" );
	}
	else
	{
		coop_log( "body: level.zombiemode_give_player_model_override is not set on this map; bodies stay those of the entity number (client numbers 4-7 get none)" );
	}
	if ( IsDefined( level.zombiemode_player_set_viewmodel_override ) )
	{
		level.coop_set_viewmodel_orig = level.zombiemode_player_set_viewmodel_override;
		level.zombiemode_player_set_viewmodel_override = ::coop_set_viewmodel;
		coop_log( "arms: wrapped level.zombiemode_player_set_viewmodel_override" );
	}
	else
	{
		coop_log( "arms: level.zombiemode_player_set_viewmodel_override is not set on this map; arms stay those of the entity number" );
	}

	players = GetPlayers();
	for ( i = 0; i < players.size; i++ )
	{
		players[i] thread coop_player_watch();
	}
	level thread coop_on_player_connect();
}


// "0 1 - 3 2" -> chars[0] = 0, chars[1] = 1, chars[3] = 3, chars[4] = 2 (no entry for "-" or anything not 0-3)
coop_parse_chars( str )
{
	chars = [];
	tokens = StrTok( str, " " );
	for ( i = 0; i < tokens.size; i++ )
	{
		if ( tokens[i] == "0" || tokens[i] == "1" || tokens[i] == "2" || tokens[i] == "3" )
		{
			chars[i] = Int( tokens[i] );
		}
	}
	return chars;
}


// the character of the player (self): the lobby's for its client number; without one the client number (retail, 0-3)
// or, for 4-7, the client number modulo 4 so the retail switch (cases 0-3) still gives a model
coop_character()
{
	num = self GetEntityNumber();
	if ( IsDefined( level.coop_chars ) && IsDefined( level.coop_chars[num] ) )
	{
		return level.coop_chars[num];
	}
	if ( num > 3 )
	{
		return num % 4;
	}
	return num;
}


// wraps level.zombiemode_give_player_model_override (the map's own function, called with the entity number)
coop_give_player_model( entity_num )
{
	num = self GetEntityNumber();
	character = self coop_character();
	self.coop_character = character;
	// a map function may read self.entity_num instead of its parameter: it sees the character during the call
	old = self.entity_num;
	self.entity_num = character;
	self [[ level.coop_give_player_model_orig ]]( character );
	if ( GetDvarInt( #"coop_entity_num" ) == 0 )
	{
		self.entity_num = old;
	}
	coop_log( "client " + num + " body of character " + character + " (the map asked for " + coop_str( entity_num ) + ", entity_num now " + coop_str( self.entity_num ) + ")" );
}


// wraps level.zombiemode_player_set_viewmodel_override
coop_set_viewmodel( entity_num )
{
	num = self GetEntityNumber();
	character = self coop_character();
	old = self.entity_num;
	self.entity_num = character;
	self [[ level.coop_set_viewmodel_orig ]]( character );
	if ( GetDvarInt( #"coop_entity_num" ) == 0 )
	{
		self.entity_num = old;
	}
	coop_log( "client " + num + " arms of character " + character + " (the map asked for " + coop_str( entity_num ) + ")" );
}


coop_on_player_connect()
{
	for ( ;; )
	{
		level waittill( "connecting", player );
		player thread coop_player_watch();
	}
}


// after every spawn: self.entity_num to the character when the map set it back to the entity number after the model
// call (the order inside the retail spawn code is not known), so voice lines use the character's
coop_player_watch()
{
	self endon( "disconnect" );
	if ( IsDefined( self.coop_watch ) )
	{
		return;
	}
	self.coop_watch = true;
	for ( ;; )
	{
		self waittill( "spawned_player" );
		wait( 0.05 );
		num = self GetEntityNumber();
		character = self coop_character();
		if ( GetDvarInt( #"coop_mod" ) == 0 || character == num )
		{
			continue;
		}
		if ( GetDvarInt( #"coop_entity_num" ) != 0 && ( !IsDefined( self.entity_num ) || self.entity_num != character ) )
		{
			coop_log( "client " + num + " entity_num " + coop_str( self.entity_num ) + " -> " + character + " after spawn" );
			self.entity_num = character;
		}
		self.coop_character = character;
	}
}


coop_str( value )
{
	if ( !IsDefined( value ) )
	{
		return "undefined";
	}
	return "" + value;
}


coop_log( msg )
{
	LogPrint( "coop: " + msg + "\n" );
}
