/* ************************************************************************
*   File: act.fight.c                                     EmpireMUD 2.0b1 *
*  Usage: non-skill commands and functions related to the fight system    *
*                                                                         *
*  EmpireMUD code base by Paul Clarke, (C) 2000-2015                      *
*  All rights reserved.  See license.doc for complete information.        *
*                                                                         *
*  EmpireMUD based upon CircleMUD 3.0, bpl 17, by Jeremy Elson.           *
*  CircleMUD (C) 1993, 94 by the Trustees of the Johns Hopkins University *
*  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
************************************************************************ */

#include "conf.h"
#include "sysdep.h"

#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "interpreter.h"
#include "handler.h"
#include "db.h"
#include "skills.h"
#include "dg_scripts.h"
#include "vnums.h"

/**
* Contents:
*   Commands
*/

// external vars
extern const char *dirs[];
extern const int universal_wait;

// external functions
void besiege_room(room_data *to_room, int damage);
void death_log(char_data *ch, char_data *killer, int type);
extern obj_data *die(char_data *ch, char_data *killer);
extern int determine_best_scale_level(char_data *ch, bool check_group);	// mobact.c
extern int get_dodge_modifier(char_data *ch);	// fight.c
extern int get_to_hit(char_data *ch, bool off_hand);	// fight.c
extern bool is_fight_ally(char_data *ch, char_data *frenemy);	// fight.c
extern bool is_fight_enemy(char_data *ch, char_data *frenemy);	// fight.c


 //////////////////////////////////////////////////////////////////////////////
//// COMMANDS ////////////////////////////////////////////////////////////////

ACMD(do_assist) {
	char_data *helpee, *opponent;

	if (FIGHTING(ch)) {
		send_to_char("You're already fighting! How can you assist someone else?\r\n", ch);
		return;
	}
	one_argument(argument, arg);

	if (!*arg)
		send_to_char("Whom do you wish to assist?\r\n", ch);
	else if (!(helpee = get_char_vis(ch, arg, FIND_CHAR_ROOM)))
		send_config_msg(ch, "no_person");
	else if (helpee == ch)
		send_to_char("You can't help yourself any more than this!\r\n", ch);
	else {
		/*
		 * Hit the same enemy the person you're helping is.
		 */
		if (FIGHTING(helpee))
			opponent = FIGHTING(helpee);
		else
			for (opponent = ROOM_PEOPLE(IN_ROOM(ch)); opponent && (FIGHTING(opponent) != helpee); opponent = opponent->next_in_room);

		if (!opponent)
			act("But nobody is fighting $M!", FALSE, ch, 0, helpee, TO_CHAR);
		else if (!CAN_SEE(ch, opponent))
			act("You can't see who is fighting $M!", FALSE, ch, 0, helpee, TO_CHAR);
		else if (FIGHT_MODE(opponent) != FMODE_MELEE)
			msg_to_char(ch, "You can't attack until they've engaged in melee combat.\r\n");
		else if (can_fight(ch, opponent)) {
			send_to_char("You join the fight!\r\n", ch);
			act("$N assists you!", 0, helpee, 0, ch, TO_CHAR);
			act("$n assists $N.", FALSE, ch, 0, helpee, TO_NOTVICT);
			hit(ch, opponent, GET_EQ(ch, WEAR_WIELD), FALSE);
		}
		else {
			act("You can't attack $N!", FALSE, ch, 0, opponent, TO_CHAR);
		}
	}
}


ACMD(do_catapult) {
	struct empire_political_data *emp_pol = NULL;
	obj_data *catapult;
	char_data *vict;
	int dir;
	empire_data *e;
	Resource rocks[2] = { {o_ROCK, 12}, END_RESOURCE_LIST };
	room_data *to_room;

	/* Find a 'pult */
	for (catapult = ROOM_CONTENTS(IN_ROOM(ch)); catapult; catapult = catapult->next_content)
		if (GET_OBJ_TYPE(catapult) == ITEM_CART && CART_CAN_FIRE(catapult))
			break;

	skip_spaces(&argument);

	if (!catapult)
		msg_to_char(ch, "You don't even have a catapult.\r\n");
	else if (!*argument)
		msg_to_char(ch, "Which direction would you like to shoot?\r\n");
	else if (!has_resources(ch, rocks, FALSE, TRUE))
		{ /* This line intentionally left blank */ }
	else if ((dir = parse_direction(ch, argument)) == NO_DIR)
		msg_to_char(ch, "Which direction is that?\r\n");
	else if (dir >= NUM_2D_DIRS || !(to_room = real_shift(IN_ROOM(ch), shift_dir[dir][0], shift_dir[dir][1])) || to_room == IN_ROOM(ch))
		msg_to_char(ch, "You can't shoot that way.\r\n");
	else if (GET_OBJ_VAL(catapult, VAL_CART_FIRING_DATA) > 1)
		msg_to_char(ch, "You must wait before shooting the catapult again.\r\n");
	else if (ROOM_SECT_FLAGGED(IN_ROOM(ch), SECTF_ROUGH))
		msg_to_char(ch, "You can't shoot from here!\r\n");
	else if (ROOM_IS_CLOSED(IN_ROOM(ch)))
		msg_to_char(ch, "You can't shoot from indoors.\r\n");
	else if (ROOM_BLD_FLAGGED(IN_ROOM(ch), BLD_BARRIER))
		msg_to_char(ch, "You can't shoot from this close to the barrier.\r\n");
	else if (IS_CITY_CENTER(to_room)) {
		msg_to_char(ch, "You can't shoot at a city center.\r\n");
	}
	else {
		if ((e = ROOM_OWNER(to_room)))
			emp_pol = find_relation(GET_LOYALTY(ch), e);
		if (e && (!emp_pol || !IS_SET(emp_pol->type, DIPL_WAR))) {
			msg_to_char(ch, "You can't attack that acre!\r\n");
			return;
			}
		extract_resources(ch, rocks, FALSE);
		sprintf(buf, "You shoot $p %s!", dirs[get_direction_for_char(ch, dir)]);
		act(buf, FALSE, ch, catapult, 0, TO_CHAR);
		
		for (vict = ROOM_PEOPLE(IN_ROOM(ch)); vict; vict = vict->next_in_room) {
			if (vict != ch && vict->desc) {
				sprintf(buf, "$n shoots $p %s!", dirs[get_direction_for_char(vict, dir)]);
				act(buf, FALSE, ch, catapult, vict, TO_VICT);
			}
		}
		
		GET_OBJ_VAL(catapult, VAL_CART_FIRING_DATA) = 2;	/* shot timer, 1 = ready to shoot */
		
		if (SHOULD_APPEAR(ch)) {
			appear(ch);
		}
		
		// fire!
		besiege_room(to_room, 8);
		WAIT_STATE(ch, 5 RL_SEC);
	}
}


ACMD(do_consider) {
	extern const char *affected_bits_consider[];
	
	char buf[MAX_STRING_LENGTH], difficult[MAX_STRING_LENGTH];
	bitvector_t bits;
	int diff, pos;
	double scale = 0.0;
	char_data *vict;
	
	one_argument(argument, arg);
	
	if (!*arg) {
		msg_to_char(ch, "Consider whom?\r\n");
	}
	else if (!(vict = get_char_vis(ch, arg, FIND_CHAR_ROOM))) {
		send_config_msg(ch, "no_person");
	}
	else if (vict == ch) {
		msg_to_char(ch, "You look pretty wimpy.\r\n");
	}
	else {
		diff = determine_best_scale_level(ch, FALSE);
		
		if (IS_NPC(vict)) {
			if (GET_CURRENT_SCALE_LEVEL(vict) == 0) {
				scale = diff;
				if (GET_MAX_SCALE_LEVEL(vict) > 0) {
					scale = MIN(GET_MAX_SCALE_LEVEL(vict), scale);
				}
				if (GET_MIN_SCALE_LEVEL(vict) > 0) {
					scale = MAX(GET_MIN_SCALE_LEVEL(vict), scale);
				}
			}
			else {
				scale = GET_CURRENT_SCALE_LEVEL(vict);
			}
			
			if (MOB_FLAGGED(vict, MOB_HARD)) {
				scale *= 1.1;
			}
			if (MOB_FLAGGED(vict, MOB_GROUP)) {
				scale *= 1.2;
			}
		}
		else {
			// player
			scale = determine_best_scale_level(vict, FALSE);
		}
		
		// compute
		diff -= (int) scale;
		
		// flag-based
		if (MOB_FLAGGED(vict, MOB_HARD) && MOB_FLAGGED(vict, MOB_GROUP)) {
			snprintf(difficult, sizeof(difficult), " (boss)");
		}
		else if (MOB_FLAGGED(vict, MOB_GROUP)) {
			snprintf(difficult, sizeof(difficult), " (group)");
		}
		else if (MOB_FLAGGED(vict, MOB_HARD)) {
			snprintf(difficult, sizeof(difficult), " (hard)");
		}
		else {
			*difficult = '\0';
		}
		
		act("$n considers $s chances against $N.", FALSE, ch, NULL, vict, TO_NOTVICT);
		act("$n considers $s chances against you.", FALSE, ch, NULL, vict, TO_VICT);
		
		if (diff < -30) {
			snprintf(buf, sizeof(buf), "$E looks like $E'd destroy you!%s", difficult);
			act(buf, FALSE, ch, NULL, vict, TO_CHAR);
		}
		else if (diff < -10) {
			snprintf(buf, sizeof(buf), "It looks like $E would be difficult for you.%s", difficult);
			act(buf, FALSE, ch, NULL, vict, TO_CHAR);
		}
		else if (diff < 10) {
			snprintf(buf, sizeof(buf), "It looks like a fair fight.%s", difficult);
			act(buf, FALSE, ch, NULL, vict, TO_CHAR);
		}
		else if (diff < 30) {
			snprintf(buf, sizeof(buf), "It looks like you wouldn't have much trouble.%s", difficult);
			act(buf, FALSE, ch, NULL, vict, TO_CHAR);
		}
		else {
			snprintf(buf, sizeof(buf), "You would walk all over $M.%s", difficult);
			act(buf, FALSE, ch, NULL, vict, TO_CHAR);
		}
		
		// flags (with overflow protection on affected_bits_consider[])
		for (bits = AFF_FLAGS(vict), pos = 0; bits && *affected_bits_consider[pos] != '\n'; bits >>= 1, ++pos) {
			if (IS_SET(bits, BIT(0)) && *affected_bits_consider[pos]) {
				snprintf(buf, sizeof(buf), "... %s", affected_bits_consider[pos]);
				act(buf, FALSE, ch, NULL, vict, TO_CHAR);
			}
		}
	}
}


ACMD(do_execute) {
	void perform_execute(char_data *ch, char_data *victim, int attacktype, int damtype);

	char_data *victim;

	one_argument(argument, arg);

	if (!*arg)
		msg_to_char(ch, "Execute whom?\r\n");
	else if (!(victim = get_char_vis(ch, arg, FIND_CHAR_ROOM)))
		send_config_msg(ch, "no_person");
	else if (victim == ch)
		msg_to_char(ch, "Seek professional help.\r\n");
	else if (GET_POS(victim) >= POS_SLEEPING && !IS_INJURED(victim, INJ_TIED | INJ_STAKED))
		act("You need to knock $M out or tie $M up.", FALSE, ch, 0, victim, TO_CHAR);
	else if (IS_DEAD(victim)) {
		msg_to_char(ch, "Your victim is already dead!\r\n");
	}
	else if (!can_fight(ch, victim)) {
		act("You can't execute $N!", FALSE, ch, NULL, victim, TO_CHAR);
	}
	else {
		perform_execute(ch, victim, TYPE_UNDEFINED, DAM_PHYSICAL);
	}
}


ACMD(do_flee) {
	extern int perform_move(char_data *ch, int dir, int need_specials_check, byte mode);
	extern const bool can_flee_dir[NUM_OF_DIRS];
	
	int i, attempt, try;
	room_data *to_room = NULL;
	char_data *was_fighting;
	bool inside = ROOM_IS_CLOSED(IN_ROOM(ch));
	struct room_direction_data *ex;

	if (GET_POS(ch) < POS_FIGHTING) {
		send_to_char("You are in pretty bad shape, unable to flee!\r\n", ch);
		return;
	}
	
	if (AFF_FLAGGED(ch, AFF_ENTANGLED)) {
		msg_to_char(ch, "You are entangled and can't flee.\r\n");
		return;
	}

	// try more times if FLEET
	for (i = 0; i < NUM_2D_DIRS * ((!IS_NPC(ch) && HAS_ABILITY(ch, ABIL_FLEET)) ? 2 : 1); i++) {
		// chance to fail if not FLEET
		if ((IS_NPC(ch) || !HAS_ABILITY(ch, ABIL_FLEET)) && number(0, 2) == 0) {
			continue;
		}

		// try 10 times to find a fleeable direction for this try
		for (try = 0, attempt = NO_DIR; try < 10 && (attempt == NO_DIR || !can_flee_dir[attempt]); ++try) {
			attempt = number(0, NUM_OF_DIRS - 1);
		}
		
		// did we find a good dir?
		if (attempt == NO_DIR || !can_flee_dir[attempt]) {
			continue;
		}
		
		// no need for to_room if not inside -- this would be a waste
		if (!inside) {
			to_room = real_shift(IN_ROOM(ch), shift_dir[attempt][0], shift_dir[attempt][1]);
			
			// did we find anything valid that direction?
			if (to_room == IN_ROOM(ch)) {
				continue;
			}
		}
		
		if ((inside && (ex = find_exit(IN_ROOM(ch), attempt)) && CAN_GO(ch, ex)) || (!inside && to_room && (!ROOM_SECT_FLAGGED(to_room, SECTF_ROUGH | SECTF_FRESH_WATER | SECTF_OCEAN) || IS_RIDING(ch)) && !ROOM_IS_CLOSED(to_room))) {
			act("$n panics, and attempts to flee!", TRUE, ch, 0, 0, TO_ROOM);
			was_fighting = FIGHTING(ch);
			if (perform_move(ch, attempt, TRUE, 0)) {
				send_to_char("You flee head over heels.\r\n", ch);
				if (was_fighting) {
					gain_ability_exp(ch, ABIL_FLEET, 5);
				}
				WAIT_STATE(ch, 2 RL_SEC);
			}
			else {
				act("$n tries to flee, but can't!", TRUE, ch, 0, 0, TO_ROOM);
				send_to_char("PANIC! You couldn't escape!\r\n", ch);
				WAIT_STATE(ch, 2 RL_SEC);
			}
			return;
		}
	}
	send_to_char("PANIC! You couldn't escape!\r\n", ch);
	WAIT_STATE(ch, 2 RL_SEC);
}


ACMD(do_hit) {
	char_data *vict;

	one_argument(argument, arg);

	if (!*arg)
		send_to_char("Hit who?\r\n", ch);
	else if (!(vict = get_char_vis(ch, arg, FIND_CHAR_ROOM)))
		send_to_char("They don't seem to be here.\r\n", ch);
	else if (vict == ch) {
		send_to_char("You hit yourself...OUCH!.\r\n", ch);
		act("$n hits $mself, and says OUCH!", FALSE, ch, 0, vict, TO_ROOM);
		}
	else if (AFF_FLAGGED(ch, AFF_CHARM) && (ch->master == vict))
		act("$N is just such a good friend, you simply can't hit $M.", FALSE, ch, 0, vict, TO_CHAR);
	else if (vict == FIGHTING(ch) && FIGHT_MODE(ch) == FMODE_MISSILE) {
		if (FIGHT_MODE(vict) == FMODE_MISSILE) {
			act("You run at $M!", FALSE, ch, 0, vict, TO_CHAR);
			FIGHT_MODE(ch) = FMODE_WAITING;
			FIGHT_WAIT(ch) = 4;
			WAIT_STATE(ch, 2 RL_SEC);
		}
		else {
			FIGHT_MODE(ch) = FMODE_MELEE;
		}
	}
	else if (can_fight(ch, vict)) {
		if (AFF_FLAGGED(ch, AFF_CHARM) && !IS_NPC(ch->master) && !IS_NPC(vict))
			return;

		if (FIGHTING(ch) == vict) {
			msg_to_char(ch, "You do the best that you can!\r\n");
		}
		else if (FIGHTING(vict) && FIGHT_MODE(vict) == FMODE_MISSILE) {
			set_fighting(ch, vict, FMODE_WAITING);
			act("You run at $M!", FALSE, ch, 0, vict, TO_CHAR);
			WAIT_STATE(ch, 2 RL_SEC);
		}
		else {
			hit(ch, vict, GET_EQ(ch, WEAR_WIELD), FALSE);
			// ensure hitting
			if (vict && !EXTRACTED(vict) && !IS_DEAD(vict) && FIGHTING(ch) && FIGHTING(ch) != vict) {
				FIGHTING(ch) = vict;
			}
			WAIT_STATE(ch, 2 RL_SEC);
		}
	}
	else {
		act("You can't attack $N!", FALSE, ch, 0, vict, TO_CHAR);
	}
}


ACMD(do_respawn) {
	extern room_data *find_load_room(char_data *ch);
	void perform_resurrection(char_data *ch, char_data *rez_by, room_data *loc, int ability);
	extern obj_data *player_death(char_data *ch);
	
	room_data *loc;
	
	if (!IS_NPC(ch) && !IS_DEAD(ch) && !IS_INJURED(ch, INJ_STAKED) && (loc = real_room(GET_RESURRECT_LOCATION(ch)))) {
		// respawn due to resurrection
		if (GET_RESURRECT_TIME(ch) + (5 * SECS_PER_REAL_MIN) < time(0)) {
			msg_to_char(ch, "Your resurrection has expired.\r\n");
		}
		else {
			perform_resurrection(ch, is_playing(GET_RESURRECT_BY(ch)), real_room(GET_RESURRECT_LOCATION(ch)), GET_RESURRECT_ABILITY(ch));
		}
	}
	else if (!IS_DEAD(ch) && !IS_INJURED(ch, INJ_STAKED)) {
		msg_to_char(ch, "You aren't even dead yet!\r\n");
	}
	else if (IS_NPC(ch)) {
		// somehow
		act("$n dies.", FALSE, ch, NULL, NULL, TO_ROOM);
		extract_char(ch);
	}
	else {
		// respawn to starting point
		msg_to_char(ch, "You shuffle off this mortal coil, and die...\r\n");
		act("$n shuffles off $s mortal coil and dies.", FALSE, ch, NULL, NULL, TO_ROOM);
		
		player_death(ch);
		char_to_room(ch, find_load_room(ch));
		GET_LAST_DIR(ch) = NO_DIR;
		
		syslog(SYS_DEATH, GET_INVIS_LEV(ch), TRUE, "%s has respawned at %s", GET_NAME(ch), room_log_identifier(IN_ROOM(ch)));
		act("$n rises from the dead!", TRUE, ch, NULL, NULL, TO_ROOM);
		look_at_room(ch);
		
		affect_total(ch);
		SAVE_CHAR(ch);
		greet_mtrigger(ch, NO_DIR);
		greet_memory_mtrigger(ch);
	}
}


ACMD(do_shoot) {
	char_data *vict;

	one_argument(argument, arg);

	if (!*arg)
		msg_to_char(ch, "Shoot whom?\r\n");
	else if (!(vict= get_char_vis(ch, arg, FIND_CHAR_ROOM)))
		send_config_msg(ch, "no_person");
	else if (vict == ch)
		msg_to_char(ch, "Shooting yourself in the foot will do you no good now.\r\n");
	else if (AFF_FLAGGED(ch, AFF_CHARM) && ch->master == vict)
		act("$N is just such a good friend, you simply can't hit $M.", FALSE, ch, 0, vict, TO_CHAR);
	else if (!GET_EQ(ch, WEAR_RANGED) || GET_OBJ_TYPE(GET_EQ(ch, WEAR_RANGED)) != ITEM_MISSILE_WEAPON)
		msg_to_char(ch, "You don't have anything to shoot!\r\n");
	else if (FIGHTING(ch))
		msg_to_char(ch, "You're already fighting for your life!\r\n");
	else if (can_fight(ch, vict)) {
		if (AFF_FLAGGED(ch, AFF_CHARM) && !IS_NPC(ch->master) && !IS_NPC(vict))
			return;

		msg_to_char(ch, "You take aim.\r\n");
		act("$n takes aim.", TRUE, ch, 0, 0, TO_ROOM);
		set_fighting(ch, vict, FMODE_MISSILE);
		if (!FIGHTING(vict) && GET_POS(vict) == POS_STANDING) {
			if (GET_EQ(vict, WEAR_RANGED) && GET_OBJ_TYPE(GET_EQ(vict, WEAR_RANGED)) == ITEM_MISSILE_WEAPON)
				set_fighting(vict, ch, FMODE_MISSILE);
			else
				set_fighting(vict, ch, FMODE_WAITING);
		}
		WAIT_STATE(ch, 2 RL_SEC);
	}
	else {
		act("You can't shoot $N!", FALSE, ch, 0, vict, TO_CHAR);
	}
}


ACMD(do_stake) {
	void scale_item_to_level(obj_data *obj, int level);
	
	char_data *victim;
	obj_data *stake;

	one_argument(argument, arg);

	if (!*arg)
		msg_to_char(ch, "%stake whom?\r\n", subcmd ? "Uns" : "S");
	else if (!(victim = get_char_vis(ch, arg, FIND_CHAR_ROOM)))
		send_config_msg(ch, "no_person");
	else if (IS_GOD(victim) || IS_IMMORTAL(victim))
		msg_to_char(ch, "You can't stake up a god!\r\n");
	else if (!subcmd && IS_INJURED(victim, INJ_STAKED))
		act("$E is already staked!", FALSE, ch, 0, victim, TO_CHAR);
	else if (subcmd && !IS_INJURED(victim, INJ_STAKED))
		act("$E isn't even staked!", FALSE, ch, 0, victim, TO_CHAR);
	else if (IS_INJURED(victim, INJ_STAKED)) {
		act("You unstake $N.", FALSE, ch, 0, victim, TO_CHAR);
		act("$n unstakes you!", FALSE, ch, 0, victim, TO_VICT | TO_SLEEP);
		act("$n unstakes $N.", FALSE, ch, 0, victim, TO_NOTVICT);
		if (GET_HEALTH(victim) > 0)
			GET_POS(victim) = POS_RESTING;
		REMOVE_BIT(INJURY_FLAGS(victim), INJ_STAKED);
		obj_to_char_or_room((stake = read_object(o_STAKE)), ch);
		if (OBJ_FLAGGED(stake, OBJ_SCALABLE)) {
			scale_item_to_level(stake, 1);	// min scale
		}
		load_otrigger(stake);
	}
	else if (!can_fight(ch, victim))
		act("You can't stake $M!", FALSE, ch, 0, victim, TO_CHAR);
	else if (!(stake = get_obj_in_list_num(o_STAKE, ch->carrying)))
		msg_to_char(ch, "You don't have a stake!\r\n");
	else if (GET_POS(victim) >= POS_SLEEPING) {
		msg_to_char(ch, "You can only stake people who have been incapacitated.\r\n");
	}
	else if (IS_DEAD(victim)) {
		msg_to_char(ch, "You can't stake someone who is already dead.\r\n");
	}
	else {
		WAIT_STATE(ch, universal_wait);

		act("You jab $p through $N's heart!", FALSE, ch, stake, victim, TO_CHAR);
		act("$n jabs $p through your heart!", FALSE, ch, stake, victim, TO_VICT | TO_SLEEP);
		act("$n jabs $p through $N's heart!", FALSE, ch, stake, victim, TO_NOTVICT);
		
		SET_BIT(INJURY_FLAGS(victim), INJ_STAKED);
		if (GET_HEALTH(victim) <= 0) {
			GET_HEALTH(victim) = 0;
			GET_POS(victim) = POS_STUNNED;
		}
		extract_obj(stake);

		if (!IS_VAMPIRE(victim)) {
			if (!IS_NPC(victim)) {
				death_log(victim, ch, ATTACK_EXECUTE);
				add_lore(ch, LORE_PLAYER_KILL, GET_IDNUM(victim));
				add_lore(victim, LORE_PLAYER_DEATH, GET_IDNUM(ch));
			}
			die(victim, ch);	// returns a corpse but we don't need it
		}
	}
}


ACMD(do_struggle) {
	if (!IS_INJURED(ch, INJ_TIED))
		msg_to_char(ch, "You aren't even bound!\r\n");
	else if (!number(0, MAX(1, GET_STRENGTH(ch)/2))) {
		msg_to_char(ch, "You struggle a bit, but fail to break free.\r\n");
		act("$n struggles a little with $s bindings!", TRUE, ch, 0, 0, TO_ROOM);
		WAIT_STATE(ch, 30 RL_SEC);
		}
	else {
		msg_to_char(ch, "You break free!\r\n");
		act("$n struggles with $s bindings and breaks free!", TRUE, ch, 0, 0, TO_ROOM);
		REMOVE_BIT(INJURY_FLAGS(ch), INJ_TIED);
	}
}


ACMD(do_summary) {
	extern char *prompt_color_by_prc(int cur, int max);
	
	char_data *iter;
	bool is_ally, is_enemy, found;
	
	found = FALSE;
	*buf = '\0';
	
	for (iter = ROOM_PEOPLE(IN_ROOM(ch)); iter; iter = iter->next_in_room) {
		is_ally = in_same_group(ch, iter) || is_fight_ally(ch, iter);
		is_enemy = is_fight_enemy(ch, iter);
		
		// any?
		found |= is_ally | is_enemy;

		if (is_ally) {
			sprintf(buf + strlen(buf), " %s:  %s ", (ch == iter) ? "You" : "Ally", PERS(iter, ch, TRUE));
			if (IS_DEAD(iter)) {
				sprintf(buf + strlen(buf), " &rDEAD&0");
			}
			else {
				sprintf(buf + strlen(buf), " %s%d&0/%d&gh&0", prompt_color_by_prc(GET_HEALTH(iter), GET_MAX_HEALTH(iter)), GET_HEALTH(iter), GET_MAX_HEALTH(iter));
				sprintf(buf + strlen(buf), " %s%d&0/%d&yv&0", prompt_color_by_prc(GET_MOVE(iter), GET_MAX_MOVE(iter)), GET_MOVE(iter), GET_MAX_MOVE(iter));
				sprintf(buf + strlen(buf), " %s%d&0/%d&cm&0", prompt_color_by_prc(GET_MANA(iter), GET_MAX_MANA(iter)), GET_MANA(iter), GET_MAX_MANA(iter));
			}
			
			if (IS_VAMPIRE(iter)) {
				sprintf(buf + strlen(buf), " %s%d&0/%d&rb&0", prompt_color_by_prc(GET_BLOOD(iter), GET_MAX_BLOOD(iter)), GET_BLOOD(iter), GET_MAX_BLOOD(iter));
			}

			if (FIGHTING(iter)) {
				sprintf(buf + strlen(buf), "  vs %s\r\n", PERS(FIGHTING(iter), ch, FALSE));
			}
			else {
				strcat(buf, "\r\n");
			}
		}
		else if (is_enemy) {
			sprintf(buf + strlen(buf), " Enemy:  %s ", PERS(iter, ch, TRUE));
			if (IS_DEAD(iter)) {
				sprintf(buf + strlen(buf), " &rDEAD&0");
			}
			else {
				sprintf(buf + strlen(buf), " %s%d%%&0h", prompt_color_by_prc(GET_HEALTH(iter), GET_MAX_HEALTH(iter)), ((int) GET_HEALTH(iter) * 100 / MAX(1, GET_MAX_HEALTH(iter))));
				sprintf(buf + strlen(buf), " %s%d%%&0v", prompt_color_by_prc(GET_MOVE(iter), GET_MAX_MOVE(iter)), ((int) GET_MOVE(iter) * 100 / MAX(1, GET_MAX_MOVE(iter))));
				sprintf(buf + strlen(buf), " %s%d%%&0m", prompt_color_by_prc(GET_MANA(iter), GET_MAX_MANA(iter)), ((int) GET_MANA(iter) * 100 / MAX(1, GET_MAX_MANA(iter))));
			}

			// no vampire stats on enemy

			if (FIGHTING(iter)) {
				sprintf(buf + strlen(buf), "  vs %s\r\n", PERS(FIGHTING(iter), ch, FALSE));
			}
			else {
				strcat(buf, "\r\n");
			}
		}
	}
	
	if (found) {
		send_to_char("Combat summary:\r\n", ch);
		send_to_char(buf, ch);
	}
	else {
		msg_to_char(ch, "You can't get a combat summary right now.\r\n");
	}
}


ACMD(do_tie) {
	void perform_npc_tie(char_data *ch, char_data *victim, int subcmd);

	char_data *victim;
	obj_data *rope;

	/* subcmd 0 = tie, 1 = untie */

	one_argument(argument, arg);

	if (!*arg)
		msg_to_char(ch, "%sie whom?\r\n", subcmd ? "Unt" : "T");
	else if (!(victim = get_char_vis(ch, arg, FIND_CHAR_ROOM)))
		send_config_msg(ch, "no_person");
	else if (IS_NPC(victim) && MOB_FLAGGED(victim, MOB_ANIMAL))
		perform_npc_tie(ch, victim, subcmd);
	else if (IS_GOD(victim) || IS_IMMORTAL(victim))
		msg_to_char(ch, "You can't tie up a god!\r\n");
	else if (!subcmd && IS_INJURED(victim, INJ_TIED))
		act("$E is already tied!", FALSE, ch, 0, victim, TO_CHAR);
	else if (subcmd && !IS_INJURED(victim, INJ_TIED))
		act("$E isn't even tied up!", FALSE, ch, 0, victim, TO_CHAR);
	else if (IS_INJURED(victim, INJ_TIED)) {
		act("You unbind $N.", FALSE, ch, 0, victim, TO_CHAR);
		act("$n unbinds you!", FALSE, ch, 0, victim, TO_VICT | TO_SLEEP);
		act("$n unbinds $N.", FALSE, ch, 0, victim, TO_NOTVICT);
		GET_HEALTH(victim) = MAX(1, GET_HEALTH(victim));
		GET_POS(victim) = POS_RESTING;
		REMOVE_BIT(INJURY_FLAGS(victim), INJ_TIED);
		obj_to_char((rope = read_object(o_ROPE)), ch);
		load_otrigger(rope);
	}
	else if (GET_POS(victim) >= POS_SLEEPING)
		act("You need to knock $M out first.", FALSE, ch, 0, victim, TO_CHAR);
	else if (!(rope = get_obj_in_list_num(o_ROPE, ch->carrying)))
		msg_to_char(ch, "You don't have any rope.\r\n");
	else {
		act("You bind and gag $N!", FALSE, ch, 0, victim, TO_CHAR);
		act("$n binds and gags you!", FALSE, ch, 0, victim, TO_VICT | TO_SLEEP);
		act("$n binds and gags $N!", FALSE, ch, 0, victim, TO_NOTVICT);
		SET_BIT(INJURY_FLAGS(victim), INJ_TIED);
		if (GET_HEALTH(victim) <= 1) {
			GET_HEALTH(victim) = 1;
			GET_POS(victim) = POS_RESTING;
		}
		extract_obj(rope);
	}
}


ACMD(do_throw) {
	extern const int rev_dir[];

	int dir = NO_DIR;
	char_data *vict;
	obj_data *obj = NULL;
	room_data *to_room = NULL;
	struct room_direction_data *ex;

	two_arguments(argument, arg, buf);

	if (!*arg || !*buf)
		msg_to_char(ch, "What would you like to throw, and which direction?\r\n");
	else if (!(obj = get_obj_in_list_vis(ch, arg, ch->carrying)))
		msg_to_char(ch, "You don't have anything like that.\r\n");
	else if ((dir = parse_direction(ch, buf)) == NO_DIR)
		msg_to_char(ch, "Which way did you want to throw it?\r\n");
	else if (ROOM_IS_CLOSED(IN_ROOM(ch))) {
		if (!(ex = find_exit(IN_ROOM(ch), dir)) || !ex->room_ptr)
			msg_to_char(ch, "You can't throw it that way.\r\n");
		else if (EXIT_FLAGGED(ex, EX_CLOSED))
			msg_to_char(ch, "You can't throw it through a closed door!\r\n");
		else
			to_room = ex->room_ptr;
	}
	else {
		if (dir >= NUM_2D_DIRS || !(to_room = real_shift(IN_ROOM(ch), shift_dir[dir][0], shift_dir[dir][1]))) {
			msg_to_char(ch, "You can't throw it that direction.\r\n");
		}
		if (to_room && ROOM_IS_CLOSED(to_room)) {
			if (BUILDING_ENTRANCE(to_room) != dir && (!ROOM_BLD_FLAGGED(to_room, BLD_TWO_ENTRANCES) || BUILDING_ENTRANCE(to_room) != rev_dir[dir])) {
				msg_to_char(ch, "You can only throw it through the entrance.\r\n");
				to_room = NULL;
			}
		}
	}
	
	// safety
	if (!to_room) {
		return;
	}

	/* If we came up with a room, lets throw! */

	sprintf(buf, "You throw $p %s as hard as you can!", dirs[get_direction_for_char(ch, dir)]);
	act(buf, FALSE, ch, obj, 0, TO_CHAR);
	
	for (vict = ROOM_PEOPLE(IN_ROOM(ch)); vict; vict = vict->next_in_room) {
		if (vict != ch && vict->desc) {
			sprintf(buf1, "$n throws $p %s as hard as $e can!", dirs[get_direction_for_char(vict, dir)]);
			act(buf1, TRUE, ch, obj, vict, TO_VICT);
		}
	}

	obj_to_room(obj, to_room);
	
	for (vict = ROOM_PEOPLE(IN_ROOM(obj)); vict; vict = vict->next_in_room) {
		if (vict->desc) {
			sprintf(buf, "$p is hurled in from the %s and falls to the ground at your feet!", dirs[get_direction_for_char(vict, rev_dir[dir])]);
			act(buf, FALSE, vict, obj, 0, TO_CHAR);
		}
	}
}
