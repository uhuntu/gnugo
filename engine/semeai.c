/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This is GNU Go, a Go program. Contact gnugo@gnu.org, or see       *
 * http://www.gnu.org/software/gnugo/ for more information.          *
 *                                                                   *
 * Copyright 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007,   *
 * 2008, 2009, 2010 and 2011 by the Free Software Foundation.        *
 *                                                                   *
 * This program is free software; you can redistribute it and/or     *
 * modify it under the terms of the GNU General Public License as    *
 * published by the Free Software Foundation - version 3 or          *
 * (at your option) any later version.                               *
 *                                                                   *
 * This program is distributed in the hope that it will be useful,   *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of    *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the     *
 * GNU General Public License in file COPYING for more details.      *
 *                                                                   *
 * You should have received a copy of the GNU General Public         *
 * License along with this program; if not, write to the Free        *
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,       *
 * Boston, MA 02111, USA.                                            *
\* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "gnugo.h"

#include <stdio.h>
#include <stdlib.h>

#include "liberty.h"

#define INFINITY 1000

static void find_moves_to_make_seki(void);
static void update_status(int dr, enum dragon_status new_status,
                          enum dragon_status new_safety);
static int close_enough_for_proper_semeai(int apos, int bpos);

/* semeai() searches for pairs of dragons of opposite color which
 * have safety DEAD. If such a pair is found, owl_analyze_semeai is
 * called to read out which dragon will prevail in a semeai, and
 * whether a move now will make a difference in the outcome. The
 * dragon statuses are revised, and if a move now will make a
 * difference in the outcome this information is stored in
 * dragon_data2 and an owl reason is later generated by
 * semeai_move_reasons().
 */

#define MAX_DRAGONS 50

void
semeai()
{
    int semeai_results_first[MAX_DRAGONS][MAX_DRAGONS];
    int semeai_results_second[MAX_DRAGONS][MAX_DRAGONS];
    int semeai_move[MAX_DRAGONS][MAX_DRAGONS];
    signed char semeai_certain[MAX_DRAGONS][MAX_DRAGONS];
    int d1, d2;
    int k;
    int num_dragons = number_of_dragons;

    if (num_dragons > MAX_DRAGONS) {
        TRACE("Too many dragons!!! Semeai analysis disabled.");
        return;
    }

    for (d1 = 0; d1 < num_dragons; d1++)
        for (d2 = 0; d2 < num_dragons; d2++) {
            semeai_results_first[d1][d2] = -1;
            semeai_results_second[d1][d2] = -1;
        }

    for (d1 = 0; d1 < num_dragons; d1++)
        for (k = 0; k < dragon2[d1].neighbors; k++) {
            int apos = DRAGON(d1).origin;
            int bpos = DRAGON(dragon2[d1].adjacent[k]).origin;
            int result_certain;

            d2 = dragon[bpos].id;

            /* Look for semeais */

            if (dragon[apos].color == dragon[bpos].color
                    || (dragon[apos].status != DEAD
                        && dragon[apos].status != CRITICAL)
                    || (dragon[bpos].status != DEAD
                        && dragon[bpos].status != CRITICAL))
                continue;


            /* Ignore inessential worms or dragons */

            if (worm[apos].inessential
                    || DRAGON2(apos).safety == INESSENTIAL
                    || worm[bpos].inessential
                    || DRAGON2(bpos).safety == INESSENTIAL)
                continue;

            /* Sometimes the dragons are considered neighbors but are too
             * distant to constitute a proper semeai, e.g. in nngs4:650, P2
             * vs. R3. Then the result of semeai reading may be meaningless
             * and can confuse the analysis. In order to avoid this we check
             * that the dragons either are directly adjacent or at least
             * have one common liberty.
             */
            if (!close_enough_for_proper_semeai(apos, bpos))
                continue;

            /* The array semeai_results_first[d1][d2] will contain the status
             * of d1 after the d1 d2 semeai, giving d1 the first move.
             * The array semeai_results_second[d1][d2] will contain the status
             * of d1 after the d1 d2 semeai, giving d2 the first move.
             */

            DEBUG(DEBUG_SEMEAI, "Considering semeai between %1m and %1m\n",
                  apos, bpos);
            owl_analyze_semeai(apos, bpos,
                               &(semeai_results_first[d1][d2]),
                               &(semeai_results_second[d1][d2]),
                               &(semeai_move[d1][d2]), &result_certain);
            DEBUG(DEBUG_SEMEAI, "results if %s moves first: %s %s, %1m%s\n",
                  board[apos] == BLACK ? "black" : "white",
                  result_to_string(semeai_results_first[d1][d2]),
                  result_to_string(semeai_results_second[d1][d2]),
                  semeai_move[d1][d2], result_certain ? "" : " (uncertain)");
            semeai_certain[d1][d2] = result_certain;
        }

    /* Look for dragons which lose all their semeais outright. The
     * winners in those semeais are considered safe and further semeais
     * they are involved in are disregarded. See semeai:81-86 and
     * nicklas5:1211 for examples of where this is useful.
     *
     * Note: To handle multiple simultaneous semeais properly we would
     * have to make simultaneous semeai reading. Lacking that we can
     * only get rough guesses of the correct status of the involved
     * dragons. This code is not guaranteed to be correct in all
     * situations but should usually be an improvement.
     */
    for (d1 = 0; d1 < num_dragons; d1++) {
        int involved_in_semeai = 0;
        int all_lost = 1;
        for (d2 = 0; d2 < num_dragons; d2++) {
            if (semeai_results_first[d1][d2] != -1) {
                involved_in_semeai = 1;
                if (semeai_results_first[d1][d2] != 0) {
                    all_lost = 0;
                    break;
                }
            }
        }

        if (involved_in_semeai && all_lost) {
            /* Leave the status changes to the main loop below. Here we just
             * remove the presumably irrelevant semeai results.
             */
            for (d2 = 0; d2 < num_dragons; d2++) {
                if (semeai_results_first[d1][d2] == 0) {
                    int d3;
                    for (d3 = 0; d3 < num_dragons; d3++) {
                        if (semeai_results_second[d3][d2] > 0) {
                            semeai_results_first[d3][d2] = -1;
                            semeai_results_second[d3][d2] = -1;
                            semeai_results_first[d2][d3] = -1;
                            semeai_results_second[d2][d3] = -1;
                        }
                    }
                }
            }
        }
    }

    for (d1 = 0; d1 < num_dragons; d1++) {
        int semeais_found = 0;
        int best_defense = 0;
        int best_attack = 0;
        int defense_move = PASS_MOVE;
        int attack_move = PASS_MOVE;
        int defense_certain = -1;
        int attack_certain = -1;
        int semeai_attack_target = NO_MOVE;
        int semeai_defense_target = NO_MOVE;

        for (d2 = 0; d2 < num_dragons; d2++) {
            if (semeai_results_first[d1][d2] == -1)
                continue;
            gg_assert(semeai_results_second[d1][d2] != -1);
            semeais_found++;

            if (best_defense < semeai_results_first[d1][d2]
                    || (best_defense == semeai_results_first[d1][d2]
                        && defense_certain < semeai_certain[d1][d2])) {
                best_defense = semeai_results_first[d1][d2];
                defense_move = semeai_move[d1][d2];
                defense_certain = semeai_certain[d1][d2];
                gg_assert(board[dragon2[d2].origin] == OTHER_COLOR(board[dragon2[d1].origin]));
                semeai_defense_target = dragon2[d2].origin;
            }
            if (best_attack < semeai_results_second[d2][d1]
                    || (best_attack == semeai_results_second[d2][d1]
                        && attack_certain < semeai_certain[d2][d1])) {
                best_attack = semeai_results_second[d2][d1];
                attack_move = semeai_move[d2][d1];
                attack_certain = semeai_certain[d2][d1];
                semeai_attack_target = dragon2[d2].origin;
            }
        }

        if (semeais_found) {
            dragon2[d1].semeais = semeais_found;
            if (best_defense != 0 && best_attack != 0)
                update_status(DRAGON(d1).origin, CRITICAL, CRITICAL);
            else if (best_attack == 0 && attack_certain)
                update_status(DRAGON(d1).origin, ALIVE, ALIVE);
            dragon2[d1].semeai_defense_code = best_defense;
            dragon2[d1].semeai_defense_point = defense_move;
            dragon2[d1].semeai_defense_certain = defense_certain;
            ASSERT1(board[semeai_defense_target]
                    == OTHER_COLOR(board[dragon2[d1].origin]),
                    dragon2[d1].origin);
            dragon2[d1].semeai_defense_target = semeai_defense_target;
            dragon2[d1].semeai_attack_code = best_attack;
            dragon2[d1].semeai_attack_point = attack_move;
            dragon2[d1].semeai_attack_certain = attack_certain;
            dragon2[d1].semeai_attack_target = semeai_attack_target;
        }
    }
    find_moves_to_make_seki();
}

/* Find moves turning supposed territory into seki. This is not
 * detected above since it either involves an ALIVE dragon adjacent to
 * a CRITICAL dragon, or an ALIVE dragon whose eyespace can be invaded
 * and turned into a seki.
 *
 * Currently we only search for tactically critical strings with
 * dragon status dead, which are neighbors of only one opponent
 * dragon, which is alive. Through semeai analysis we then determine
 * whether such a string can in fact live in seki. Relevant testcases
 * include gunnar:42 and gifu03:2.
 */
static void
find_moves_to_make_seki()
{
    int str;
    int defend_move;
    int resulta, resultb;

    for (str = BOARDMIN; str < BOARDMAX; str++) {
        if (IS_STONE(board[str]) && is_worm_origin(str, str)
                && attack_and_defend(str, NULL, NULL, NULL, &defend_move)
                && dragon[str].status == DEAD
                && DRAGON2(str).hostile_neighbors == 1) {
            int k;
            int color = board[str];
            int opponent = NO_MOVE;
            int certain;
            struct eyevalue reduced_genus;

            for (k = 0; k < DRAGON2(str).neighbors; k++) {
                opponent = dragon2[DRAGON2(str).adjacent[k]].origin;
                if (board[opponent] != color)
                    break;
            }

            ASSERT1(opponent != NO_MOVE, opponent);

            if (dragon[opponent].status != ALIVE)
                continue;

            /* FIXME: These heuristics are used for optimization.  We don't
             *        want to call expensive semeai code if the opponent
             *        dragon has more than one eye elsewhere.  However, the
             *        heuristics might still need improvement.
             */
            compute_dragon_genus(opponent, &reduced_genus, str);

            if (min_eyes(&reduced_genus) > 1
                    || DRAGON2(opponent).moyo_size > 10
                    || DRAGON2(opponent).moyo_territorial_value > 2.999
                    || DRAGON2(opponent).escape_route > 0
                    || DRAGON2(str).escape_route > 0)
                continue;

            owl_analyze_semeai_after_move(defend_move, color, opponent, str,
                                          &resulta, &resultb, NULL, &certain, 0);

            if (resultb == WIN) {
                owl_analyze_semeai(str, opponent, &resultb, &resulta,
                                   &defend_move, &certain);
                resulta = REVERSE_RESULT(resulta);
                resultb = REVERSE_RESULT(resultb);
            }

            /* Do not trust uncertain results. In fact it should only take a
             * few nodes to determine the semeai result, if it is a proper
             * potential seki position.
             */
            if (resultb != WIN && certain) {
                int d = dragon[str].id;
                DEBUG(DEBUG_SEMEAI, "Move to make seki at %1m (%1m vs %1m)\n",
                      defend_move, str, opponent);
                dragon2[d].semeais++;
                update_status(str, CRITICAL, CRITICAL);
                dragon2[d].semeai_defense_code = REVERSE_RESULT(resultb);
                dragon2[d].semeai_defense_point = defend_move;
                dragon2[d].semeai_defense_certain = certain;
                gg_assert(board[opponent] == OTHER_COLOR(board[dragon2[d].origin]));
                dragon2[d].semeai_defense_target = opponent;

                /* We need to determine a proper attack move (the one that
                 * prevents seki).  Currently we try the defense move first,
                 * and if it doesn't work -- all liberties of the string.
                 */
                owl_analyze_semeai_after_move(defend_move, OTHER_COLOR(color),
                                              str, opponent, &resulta, NULL,
                                              NULL, NULL, 0);
                if (resulta != WIN) {
                    dragon2[d].semeai_attack_code = REVERSE_RESULT(resulta);
                    dragon2[d].semeai_attack_point = defend_move;
                }
                else {
                    int k;
                    int libs[MAXLIBS];
                    int liberties = findlib(str, MAXLIBS, libs);

                    for (k = 0; k < liberties; k++) {
                        owl_analyze_semeai_after_move(libs[k], OTHER_COLOR(color),
                                                      str, opponent, &resulta, NULL,
                                                      NULL, NULL, 0);
                        if (resulta != WIN) {
                            dragon2[d].semeai_attack_code = REVERSE_RESULT(resulta);
                            dragon2[d].semeai_attack_point = libs[k];
                            break;
                        }
                    }

                    if (k == liberties) {
                        DEBUG(DEBUG_SEMEAI,
                              "No move to attack in semeai (%1m vs %1m), seki assumed.\n",
                              str, opponent);
                        dragon2[d].semeai_attack_code = 0;
                        dragon2[d].semeai_attack_point = NO_MOVE;
                        update_status(str, ALIVE, ALIVE_IN_SEKI);
                    }
                }

                DEBUG(DEBUG_SEMEAI, "Move to prevent seki at %1m (%1m vs %1m)\n",
                      dragon2[d].semeai_attack_point, opponent, str);

                dragon2[d].semeai_attack_certain = certain;
                dragon2[d].semeai_attack_target = opponent;
            }
        }
    }

    /* Now look for dead strings inside a single eyespace of a living dragon.
     *
     * FIXME: Clearly this loop should share most of its code with the
     *        one above. It would also be good to reimplement so that
     *        moves invading a previously empty single eyespace to make
     *        seki can be found.
     */
    for (str = BOARDMIN; str < BOARDMAX; str++) {
        if (IS_STONE(board[str]) && is_worm_origin(str, str)
                && !find_defense(str, NULL)
                && dragon[str].status == DEAD
                && DRAGON2(str).hostile_neighbors == 1) {
            int k;
            int color = board[str];
            int opponent = NO_MOVE;
            int certain;
            struct eyevalue reduced_genus;

            for (k = 0; k < DRAGON2(str).neighbors; k++) {
                opponent = dragon2[DRAGON2(str).adjacent[k]].origin;
                if (board[opponent] != color)
                    break;
            }

            ASSERT1(opponent != NO_MOVE, opponent);

            if (dragon[opponent].status != ALIVE)
                continue;

            /* FIXME: These heuristics are used for optimization.  We don't
             *        want to call expensive semeai code if the opponent
             *        dragon has more than one eye elsewhere.  However, the
             *        heuristics might still need improvement.
             */
            compute_dragon_genus(opponent, &reduced_genus, str);
            if (DRAGON2(opponent).moyo_size > 10 || min_eyes(&reduced_genus) > 1)
                continue;

            owl_analyze_semeai(str, opponent, &resulta, &resultb,
                               &defend_move, &certain);

            /* Do not trust uncertain results. In fact it should only take a
             * few nodes to determine the semeai result, if it is a proper
             * potential seki position.
             */
            if (resulta != 0 && certain) {
                int d = dragon[str].id;
                DEBUG(DEBUG_SEMEAI, "Move to make seki at %1m (%1m vs %1m)\n",
                      defend_move, str, opponent);
                dragon2[d].semeais++;
                update_status(str, CRITICAL, CRITICAL);
                dragon2[d].semeai_defense_code = resulta;
                dragon2[d].semeai_defense_point = defend_move;
                dragon2[d].semeai_defense_certain = certain;
                gg_assert(board[opponent] == OTHER_COLOR(board[dragon2[d].origin]));
                dragon2[d].semeai_defense_target = opponent;

                /* We need to determine a proper attack move (the one that
                 * prevents seki).  Currently we try the defense move first,
                 * and if it doesn't work -- all liberties of the string.
                 */
                owl_analyze_semeai_after_move(defend_move, OTHER_COLOR(color),
                                              str, opponent, &resulta, NULL,
                                              NULL, NULL, 0);
                if (resulta != WIN) {
                    dragon2[d].semeai_attack_code = REVERSE_RESULT(resulta);
                    dragon2[d].semeai_attack_point = defend_move;
                }
                else {
                    int k;
                    int libs[MAXLIBS];
                    int liberties = findlib(str, MAXLIBS, libs);

                    for (k = 0; k < liberties; k++) {
                        owl_analyze_semeai_after_move(libs[k], OTHER_COLOR(color),
                                                      str, opponent, &resulta, NULL,
                                                      NULL, NULL, 0);
                        if (resulta != WIN) {
                            dragon2[d].semeai_attack_code = REVERSE_RESULT(resulta);
                            dragon2[d].semeai_attack_point = libs[k];
                            break;
                        }
                    }

                    if (k == liberties) {
                        DEBUG(DEBUG_SEMEAI,
                              "No move to attack in semeai (%1m vs %1m), seki assumed.\n",
                              str, opponent);
                        dragon2[d].semeai_attack_code = 0;
                        dragon2[d].semeai_attack_point = NO_MOVE;
                        update_status(str, ALIVE, ALIVE_IN_SEKI);
                    }
                }

                DEBUG(DEBUG_SEMEAI, "Move to prevent seki at %1m (%1m vs %1m)\n",
                      dragon2[d].semeai_attack_point, opponent, str);

                dragon2[d].semeai_attack_certain = certain;
                dragon2[d].semeai_attack_target = opponent;
            }
        }
    }
}


/* neighbor_of_dragon(pos, origin) returns true if the vertex at (pos) is a
 * neighbor of the dragon with origin at (origin).
 */
static int
neighbor_of_dragon(int pos, int origin)
{
    int k;
    if (pos == NO_MOVE)
        return 0;

    for (k = 0; k < 4; k++)
        if (ON_BOARD(pos + delta[k]) && dragon[pos + delta[k]].origin == origin)
            return 1;

    return 0;
}

/* Check whether two dragons are directly adjacent or have at least
 * one common liberty.
 */
static int
close_enough_for_proper_semeai(int apos, int bpos)
{
    int pos;
    for (pos = BOARDMIN; pos < BOARDMAX; pos++) {
        if (board[pos] == EMPTY
                && neighbor_of_dragon(pos, apos)
                && neighbor_of_dragon(pos, bpos))
            return 1;
        else if (IS_STONE(board[pos])) {
            if (is_same_dragon(pos, apos) && neighbor_of_dragon(pos, bpos))
                return 1;
            if (is_same_dragon(pos, bpos) && neighbor_of_dragon(pos, apos))
                return 1;
        }
    }

    return 0;
}

/* This function adds the semeai related move reasons, using the information
 * stored in the dragon2 array.
 *
 * If the semeai had an uncertain result, and there is a owl move with
 * certain result doing the same, we don't trust the semeai move.
 */
void
semeai_move_reasons(int color)
{
    int other = OTHER_COLOR(color);
    int d;
    int liberties;
    int libs[MAXLIBS];
    int r;

    for (d = 0; d < number_of_dragons; d++)
        if (dragon2[d].semeais && DRAGON(d).status == CRITICAL) {
            if (DRAGON(d).color == color
                    && dragon2[d].semeai_defense_point
                    && (dragon2[d].owl_defense_point == NO_MOVE
                        || dragon2[d].semeai_defense_certain >=
                        dragon2[d].owl_defense_certain)) {
                /* My dragon can be defended. */
                add_semeai_move(dragon2[d].semeai_defense_point, dragon2[d].origin);
                DEBUG(DEBUG_SEMEAI, "Adding semeai defense move for %1m at %1m\n",
                      DRAGON(d).origin, dragon2[d].semeai_defense_point);
                if (neighbor_of_dragon(dragon2[d].semeai_defense_point,
                                       dragon2[d].semeai_defense_target)
                        && !neighbor_of_dragon(dragon2[d].semeai_defense_point,
                                               dragon2[d].origin)
                        && !is_self_atari(dragon2[d].semeai_defense_point, color)) {

                    /* If this is a move to fill the non-common liberties of the
                     * target, and is not a ko or snap-back, then we mark all
                     * non-common liberties of the target as potential semeai moves.
                     */

                    liberties = findlib(dragon2[d].semeai_defense_target, MAXLIBS, libs);

                    for (r = 0; r < liberties; r++) {
                        if (!neighbor_of_dragon(libs[r], dragon2[d].origin)
                                && !is_self_atari(libs[r], color)
                                && libs[r] != dragon2[d].semeai_defense_point)
                            add_potential_semeai_defense(libs[r], dragon2[d].origin,
                                                         dragon2[d].semeai_defense_target);
                    }
                }
            }
            else if (DRAGON(d).color == other
                     && dragon2[d].semeai_attack_point
                     && (dragon2[d].owl_attack_point == NO_MOVE
                         || dragon2[d].owl_defense_point == NO_MOVE
                         || dragon2[d].semeai_attack_certain >=
                         dragon2[d].owl_attack_certain)) {
                /* Your dragon can be attacked. */
                add_semeai_move(dragon2[d].semeai_attack_point, dragon2[d].origin);
                DEBUG(DEBUG_SEMEAI, "Adding semeai attack move for %1m at %1m\n",
                      DRAGON(d).origin, dragon2[d].semeai_attack_point);
                if (neighbor_of_dragon(dragon2[d].semeai_attack_point,
                                       dragon2[d].origin)
                        && !neighbor_of_dragon(dragon2[d].semeai_attack_point,
                                               dragon2[d].semeai_attack_target)
                        && !is_self_atari(dragon2[d].semeai_attack_point, color)) {

                    liberties = findlib(dragon2[d].origin, MAXLIBS, libs);

                    for (r = 0; r < liberties; r++) {
                        if (!neighbor_of_dragon(libs[r], dragon2[d].semeai_attack_target)
                                && !is_self_atari(libs[r], color)
                                && libs[r] != dragon2[d].semeai_attack_point)
                            add_potential_semeai_attack(libs[r], dragon2[d].origin,
                                                        dragon2[d].semeai_attack_target);
                    }
                }
            }
        }
}


/* Change the status and safety of a dragon.  In addition, if the new
 * status is not DEAD, make all worms of the dragon essential, so that
 * results found by semeai code don't get ignored.
 */
static void
update_status(int dr, enum dragon_status new_status,
              enum dragon_status new_safety)
{
    int pos;

    if (dragon[dr].status != new_status
            && (dragon[dr].status != CRITICAL || new_status != DEAD)) {
        DEBUG(DEBUG_SEMEAI, "Changing status of %1m from %s to %s.\n", dr,
              status_to_string(dragon[dr].status),
              status_to_string(new_status));
        for (pos = BOARDMIN; pos < BOARDMAX; pos++)
            if (IS_STONE(board[pos]) && is_same_dragon(dr, pos)) {
                dragon[pos].status = new_status;
                if (new_status != DEAD)
                    worm[pos].inessential = 0;
            }
    }

    if (DRAGON2(dr).safety != new_safety
            && (DRAGON2(dr).safety != CRITICAL || new_safety != DEAD)) {
        DEBUG(DEBUG_SEMEAI, "Changing safety of %1m from %s to %s.\n", dr,
              status_to_string(DRAGON2(dr).safety), status_to_string(new_safety));
        DRAGON2(dr).safety = new_safety;
    }
}


/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
