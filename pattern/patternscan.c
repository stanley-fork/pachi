#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "move.h"
#include "random.h"
#include "pattern/pattern.h"
#include "pattern/spatial.h"
#include "pattern/patternscan.h"


/* The engine has two modes:
 *
 * - gen_spat_dict=1: generate patterns_mm.spat file from encountered spatials.
 *                    only consider spatials from played moves.
 *
 * - gen_spat_dict=0: generate output for mm tool
 *       each move is pattern matched into team of features which can be fed
 *       into mm tool to compute gammas.
 */

/* Internal engine state. */
typedef struct {
	int debug_level;

	pattern_config_t pc;
	bool spat_split_sizes;
	int color_mask;

	bool gen_spat_dict;
	bool mcowner_fast;
	int spat_threshold;	  /* Minimal number of occurences for spatial to be saved. */	
	int loaded_spatials;      /* Number of loaded spatials; checkpoint for saving new sids
				   * in case gen_spat_dict is enabled. */

	strbuf_t buf;

	/* Book-keeping of spatial occurence count. */
	int gameno;
	unsigned int nscounts;
	int *scounts;
	//int *sgameno;
} patternscan_t;

/* Visualize spatials ? */
//#define DEBUG_GENSPATIAL 1

/* Make patternscan's mm mode output more explicit.
 * (for debugging purposes, can't feed it into mm tool then) */
//#define DEBUG_MM 1


#define PATTERNSCAN_BUF_LEN  1048576

static patternscan_t *global_ps = 0;
static feature_info_t *features = pattern_features;

static void
mm_print_feature(patternscan_t *ps, strbuf_t *buf, feature_t *f)
{
	assert(f->id >= 0 && f->id < FEAT_MAX);
	
	sbprintf(buf, "%i", feature_gamma_number(f));
#ifdef DEBUG_MM
	sbprintf(buf, "(%s:%i)", features[f->id].name, f->payload);
#endif
}

static void
mm_print_pattern(patternscan_t *ps, strbuf_t *buf, pattern_t *p)
{
	for (int i = 0; i < p->n; i++) {
		if (i)  sbprintf(buf, " ");
		mm_print_feature(ps, buf, &p->f[i]);
	}
	sbprintf(buf, "\n");
}

static void
mm_header(patternscan_t *ps)
{
	/* Number of gammas */
	printf("! %i\n", pattern_gammas());

	/* Number of features */
	printf("%i\n", FEAT_MAX);

	/* Number of gammas for each feature */
	for (int i = 0; i < FEAT_MAX; i++)
		printf("%i %s\n", feature_payloads(i), features[i].name);
	
	printf("!\n");
}

static void
mm_table(patternscan_t *ps)
{
	FILE *file = fopen("mm-pachi.table", "w");  assert(file);
	
	for (int i = 0; i < FEAT_MAX; i++) {
		feature_t f = feature(i, 0);
		for (unsigned int j = 0; j < feature_payloads(i); j++) {
			f.payload = j;
			fprintf(file, "%i (%s)\n", feature_gamma_number(&f), feature2sstr(&f));
		}
	}
	
	fclose(file);
}


static void
patternscan_mm_init(patternscan_t *ps)
{
	/* mm header */
	mm_header(ps);
	
	/* write mm-pachi.table: feature to mm mapping */
	mm_table(ps);
}


typedef void (*process_func_t)(patternscan_t *ps, board_t *b, move_t *m,
			       strbuf_t *buf, bool game_move, void *data);

static void
process_pattern(patternscan_t *ps, board_t *b, move_t *m,
		bool game_move, process_func_t callback, void *data)
{
	callback(ps, b, m, &ps->buf, game_move, data);

	/* Go through other moves as well */
	if (game_move) {
		foreach_free_point(b) {
			move_t m2 = move(c, m->color);
			if (c == m->coord)                                           continue;
			if (!board_is_valid_play_no_suicide(b, m2.color, m2.coord))  continue;
			process_pattern(ps, b, &m2, false, callback, data);
		} foreach_free_point_end;
	}
}

static void
mm_process_move(patternscan_t *ps, board_t *b, move_t *m, strbuf_t *buf,
		bool game_move, void *data)
{
	pattern_context_t *ct = (pattern_context_t*)data;
	
	/* Now, match the pattern. */
	pattern_t p;
	pattern_match(b, m, &p, ct, true);

	if (game_move) {
		sbprintf(buf, "#\n");
		mm_print_pattern(ps, buf, &p);
		mm_print_pattern(ps, buf, &p); /* mm needs winner team also in the participants */
	}
	else    mm_print_pattern(ps, buf, &p);
}

/* Store the spatial configuration in dictionary if applicable. */
static void
genspatial_process_move(patternscan_t *ps, board_t *b, move_t *m, strbuf_t *buf,
			bool game_move, void *data)
{
	if (is_pass(m->coord))  return;
	if (!game_move) return;		/* Only save patterns from played moves */

#ifdef DEBUG_GENSPATIAL
	fprintf(stderr, "--------------------------------------------------------------\n\n");
	coord_t last_move = last_move(b).coord;
	last_move(b).coord = m->coord;
	board_print(b, stderr);
	fprintf(stderr, "%s to play\n", stone2str(board_to_play(b)));
	last_move(b).coord = last_move;
#endif

	spatial_t s;
	spatial_from_board(&ps->pc, &s, b, m);
	int dmax = s.dist;
	for (int d = ps->pc.spat_min; d <= dmax; d++) {
		s.dist = d;
		unsigned int sid = spatial_dict_add(&s);
#define SCOUNTS_ALLOC 1048576 // Allocate space in 1M*4 blocks.
		if (sid >= ps->nscounts) {
			int newnsc = (sid / SCOUNTS_ALLOC + 1) * SCOUNTS_ALLOC;
			ps->scounts = (int*)realloc(ps->scounts, newnsc * sizeof(*ps->scounts));
			memset(&ps->scounts[ps->nscounts], 0, (newnsc - ps->nscounts) * sizeof(*ps->scounts));
			//ps->sgameno = realloc(ps->sgameno, newnsc * sizeof(*ps->sgameno));
			//memset(&ps->sgameno[ps->nscounts], 0, (newnsc - ps->nscounts) * sizeof(*ps->sgameno));
			ps->nscounts = newnsc;
		}
		
		/* Show stats from time to time */
		if (ps->debug_level > 1 && !fast_random(65536) && !fast_random(32))
			fprintf(stderr, "%d spatials\n", spat_dict->nspatials);
			
		/* Global pattern count (including multiple hits per game) */
		ps->scounts[sid]++;
			
#ifdef DEBUG_GENSPATIAL
		fprintf(stderr, "id=%u d=%i hits=%i %s\n\n", sid, s.dist, ps->scounts[sid], spatial2str(&s));
		spatial_print(b, stderr, &s, m->coord);
#endif
			
		static int gameno = 0;
		if (ps->gameno > gameno + 5) {
			gameno = ps->gameno;
			fprintf(stderr, "\t\t\tgames: %-15i spatials stored: %u\n", gameno, spat_dict->nspatials);
		}
	}
}

static char *
patternscan_play(engine_t *e, board_t *b, move_t *m, char *enginearg, bool *board_print)
{
	patternscan_t *ps = (patternscan_t*)e->data;

	if (is_pass(m->coord))
		return NULL;
	/* Deal with broken game records that sometimes get fed in. */
	assert(board_at(b, m->coord) == S_NONE);

	if (b->moves == b->handicap + 1)
		ps->gameno++;

	if (!(m->color & ps->color_mask))
		return NULL;
	/* The user can request this play to be "silent", to get patterns
	 * only for a single specific situation. */
	if (enginearg && *enginearg == '0')
		return NULL;

	/* Reset string buffer */
	strbuf_init(&ps->buf, ps->buf.str, PATTERNSCAN_BUF_LEN);

	/* Process patterns for this move. */
	if (ps->gen_spat_dict)
		process_pattern(ps, b, m, true, genspatial_process_move, NULL);
	else {
		ownermap_t ownermap;
		if (ps->mcowner_fast)  mcowner_playouts_fast(b, m->color, &ownermap);
		else		       mcowner_playouts(b, m->color, &ownermap); /* slooow */
		
		pattern_context_t ct;
		pattern_context_init(&ct, &ps->pc, &ownermap);

		process_pattern(ps, b, m, true, mm_process_move, &ct);
	}

	return ps->buf.str;
}

static coord_t
patternscan_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	die("genmove command not available during patternscan!\n");
}

/* Sort spatials by distance + frequency */
static int
compare_spatials(const void *p1, const void *p2)
{
	unsigned int id1 = *(unsigned int*)p1;
	unsigned int id2 = *(unsigned int*)p2;
	spatial_t *s1 = get_spatial(id1);
	spatial_t *s2 = get_spatial(id2);
	
	/* Sort by distance */
	if (s1->dist != s2->dist)
		return (s1->dist - s2->dist);
	
	/* Then by pattern frequency */
	return (global_ps->scounts[id2] - global_ps->scounts[id1]);
}

/* genspatial: write spatial dictionary. */
static void
genspatial_done(patternscan_t *ps)
{
	bool newfile = !file_exists(spatial_dict_filename);
	FILE *f = fopen(spatial_dict_filename, "a");
	if (newfile)
		spatial_dict_writeinfo(f);

	/* Sort new spatials by distance and frequency (most frequent first). */
#define MATCHES_ALLOC 65536
	unsigned int *matches = 0;
	unsigned int nmatches = 0;
 
	for (unsigned int i = ps->loaded_spatials; i < spat_dict->nspatials; i++) {
		/* By default, threshold is 0 and condition is always true. */
		assert(i < ps->nscounts && ps->scounts[i] > 0);
		if (ps->scounts[i] >= ps->spat_threshold) {
			if (!(nmatches % MATCHES_ALLOC))
				matches = (unsigned int*)realloc(matches, (nmatches + MATCHES_ALLOC) * sizeof(*matches));
			matches[nmatches++] = i;
		}
	}

	qsort(matches, nmatches, sizeof(*matches), compare_spatials);
	
	for (unsigned int j = 0; j < nmatches; j++) {
		unsigned int id = matches[j];
		unsigned int newid = ps->loaded_spatials + j;
		spatial_t *s = get_spatial(id);
		spatial_write(s, newid, f);

		/* Stats */
		fprintf(stderr, "hits=%-6i   id=%-6i    d=%-2i    %s\n", ps->scounts[id], id, s->dist, spatial2str(s));
	}
	fprintf(stderr, "Added %u patterns\n", nmatches);

	unsigned int scanned_patterns = 0;
	for (unsigned int i = 0; i < spat_dict->nspatials; i++)
		scanned_patterns += ps->scounts[i];
	fprintf(stderr, "Processed %u patterns\n",  scanned_patterns);

	fclose(f);
}

static void
patternscan_done(engine_t *e)
{
	patternscan_t *ps = (patternscan_t*)e->data;
	
	if (ps->gen_spat_dict)
		genspatial_done(ps);

	free(ps->buf.str);     ps->buf.str = NULL;
}

#define NEED_RESET   ENGINE_SETOPTION_NEED_RESET
#define option_error engine_setoption_error

static bool
patternscan_setoption(engine_t *e, board_t *b, const char *optname, char *optval,
		      char **err, bool setup, bool *reset)
{
	static_strbuf(ebuf, 256);
	patternscan_t *ps = (patternscan_t*)e->data;

	if (!strcasecmp(optname, "debug")) {
		if (optval)  ps->debug_level = atoi(optval);
		else         ps->debug_level++;
	}
	else if (!strcasecmp(optname, "gen_spat_dict")) {
		/* If set, re-generate the spatial patterns
		 * dictionary; you need to have a dictionary
		 * of spatial stone configurations in order
		 * to match any spatial features. */
		/* XXX: If you specify the 'patterns' option,
		 * this must come first! */
		ps->gen_spat_dict = !optval || atoi(optval);
	}
	else if (!strcasecmp(optname, "spat_threshold") && optval) {
		/* Minimal number of times new spatial
		 * feature must occur in this run (!) to
		 * be included in the dictionary. */
		ps->spat_threshold = atoi(optval);
	}
	else if (!strcasecmp(optname, "spat_split_sizes")) {
		/* Generate a separate pattern for each
		 * spatial size. This is important to
		 * preserve good generalization in unknown
		 * situations where the largest pattern
		 * might not match. */
		ps->spat_split_sizes = 1;
	}
	else if (!strcasecmp(optname, "color_mask") && optval) {
		/* Bitmask of move colors to match. Set this
		 * to 2 if you want to match only white moves,
		 * for example. (Useful for processing
		 * handicap games.) */
		ps->color_mask = atoi(optval);
	}
	else if (!strcasecmp(optname, "mcowner_fast") && optval) {
		/* Use mcowner_fast=0 for better ownermap accuracy
		 * when generating mm patterns. Will take hours though.
		 * Default: mcowner_fast=1 */
		ps->mcowner_fast = atoi(optval);
	}
	else if (!strcasecmp(optname, "patterns") && optval) {  NEED_RESET
		patterns_init(&ps->pc, optval, ps->gen_spat_dict, false);
	}
	else
		option_error("patternscan: Invalid engine argument %s or missing value\n", optname);

	return true;
}

static patternscan_t *
patternscan_state_init(engine_t *e, board_t *b)
{
	options_t *options = &e->options;
	patternscan_t *ps = global_ps = calloc2(1, patternscan_t);
	e->data = ps;
	
	bool pat_setup = false;

	ps->debug_level = 1;
	ps->color_mask = S_BLACK | S_WHITE;

	/* Default mode: match patterns and generate output for mm tool. */
	ps->spat_split_sizes = 1;
	ps->mcowner_fast = true;

	/* Process engine options. */
	for (int i = 0; i < options->n; i++) {
		char *err;
		if (!engine_setoption(e, b, &options->o[i], &err, true, NULL))
			die("%s", err);
		if (!strcmp(options->o[i].name, "patterns"))
			pat_setup = true;
	}

#ifndef GENSPATIAL
	if (ps->gen_spat_dict)  die("recompile with -DGENSPATIAL to generate spatial dictionary.\n");
#endif

	if (!pat_setup)		   patterns_init(&ps->pc, NULL, ps->gen_spat_dict, false);
	if (ps->spat_split_sizes)  ps->pc.spat_largest = 0;
	ps->loaded_spatials = spat_dict->nspatials;
	ps->gameno = 1;
	
	if (!ps->gen_spat_dict)    patternscan_mm_init(ps);
	strbuf_init_alloc(&ps->buf, PATTERNSCAN_BUF_LEN);
	return ps;
}

void
patternscan_engine_init(engine_t *e, board_t *b)
{
	e->name = "PatternScan";
	e->comment = "You cannot play Pachi with this engine, it is intended for special development use - scanning of games fed to it as GTP streams for various pattern features.";
	e->genmove = patternscan_genmove;
	e->setoption = patternscan_setoption;
	e->notify_play = patternscan_play;
	e->done = patternscan_done;
	// clear_board does not concern us, we like to work over many games
	e->keep_on_clear = true;

	patternscan_state_init(e, b);
}
