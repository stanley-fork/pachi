#ifndef PACHI_GTP_H
#define PACHI_GTP_H

#include "board.h"
#include "timeinfo.h"

struct engine;

enum parse_code {
	P_OK,
	P_ENGINE_RESET,
	P_UNKNOWN_COMMAND,
	
	/* For engines notify() handlers: */
	
	P_NOREPLY,		/* run default handler but suppress output */
	P_DONE_OK,		/* override, don't run default handler */
};

typedef struct
{
	/* Gtp Options */
	bool		noundo;			/* undo only allowed for pass */
	bool		kgs;			/* kgs mode */
	bool		kgs_chat;		/* enable kgs-chat command ? */
	bool		fatal;			/* abort on gtp error */
	char*		custom_name;
	char*		banner;			/* kgs game start message */
	
	/* Private fields (global) */
	int		played_games;
	move_history_t	history;		/* move history, for undo */
	bool		undo_pending;
	bool		analyze_mode;		/* analyze mode / genmove mode */
	bool		analyze_running;

	/* Single cmd scope: */
	char*		cmd;
	char*		next;
	int		id;
	bool		quiet;			/* mute all gtp output */
	bool		replied;		/* gtp reply sent */
	bool		flushed;		/* gtp_flush() called */
	bool		error;			/* gtp_error() / gtp_error_printf() called */
} gtp_t;

#define gtp_arg_next(gtp) \
	gtp->next = gtp->next + strcspn(gtp->next, " \t\r\n"); \
	if (*gtp->next) { \
		*gtp->next = 0; gtp->next++; \
		gtp->next += strspn(gtp->next, " \t\r\n"); \
	}

#define gtp_arg_optional(arg)  do {  \
	(arg) = gtp->next;  \
	gtp_arg_next(gtp);  \
} while(0)

#define gtp_arg(arg)  do { \
	(arg) = gtp->next; \
	if (!*(arg)) {	\
		gtp_error(gtp, "argument missing"); \
		return P_OK; \
	} \
	gtp_arg_next(gtp); \
} while(0)

#define gtp_arg_number(n)   do {  \
	char *arg_;  \
	gtp_arg(arg_);  \
	if (!valid_number(arg_)) {	 \
	        if (DEBUGL(0)) fprintf(stderr, "Invalid numeric value '%s'\n", arg_);  \
		gtp_error(gtp, "numeric value expected"); \
		return P_OK; \
	} \
	(n) = atoi(arg_);  \
} while(0)

#define gtp_arg_float(x)   do {  \
	char *arg_;  \
	gtp_arg(arg_);  \
	if (!valid_float(arg_)) {  \
	        if (DEBUGL(0)) fprintf(stderr, "Invalid numeric value '%s'\n", arg_);  \
		gtp_error(gtp, "numeric value expected"); \
		return P_OK; \
	} \
	float *px = &(x);  \
	sscanf(arg_, "%f", px);  \
} while(0)

#define gtp_arg_color(color)   do {  \
	char *arg_;  \
	gtp_arg(arg_);  \
	if (!valid_color(arg_)) {  \
		if (DEBUGL(0)) fprintf(stderr, "Invalid color '%s'\n", arg_);  \
		gtp_error(gtp, "invalid coord");	\
		return P_OK; \
	} \
	(color) = str2stone(arg_);  \
} while(0)

#define gtp_arg_coord(c)   do {  \
	char *arg_;  \
	gtp_arg(arg_);  \
	if (!valid_coord(arg_)) {  \
		if (DEBUGL(0)) fprintf(stderr, "Invalid coord '%s'\n", arg_);  \
		gtp_error(gtp, "invalid coord");	\
		return P_OK; \
	} \
	(c) = str2coord(arg_);  \
} while(0)


void gtp_init(gtp_t *gtp, board_t *b);
void gtp_done(gtp_t *gtp);

void gtp_internal_init(gtp_t *gtp);
enum parse_code gtp_parse(gtp_t *gtp, board_t *b, struct engine *e, time_info_t *ti, char *buf);

/* Output one line, end-of-line \n added automatically. */
void gtp_reply(gtp_t *gtp, const char *str);
void gtp_error(gtp_t *gtp, const char *str);

/* Output anything (no \n added). 
 * Can just use printf() after first gtp_printf() */
void gtp_printf(gtp_t *gtp, const char *format, ...)
	__attribute__ ((format (printf, 2, 3)));
void gtp_error_printf(gtp_t *gtp, const char *format, ...)
	__attribute__ ((format (printf, 2, 3)));

#define is_gamestart(cmd) (!strcasecmp((cmd), "boardsize"))
#define is_reset(cmd) (is_gamestart(cmd) || !strcasecmp((cmd), "clear_board") || !strcasecmp((cmd), "kgs-rules"))
#define is_repeated(cmd) (strstr((cmd), "pachi-genmoves"))


#endif
