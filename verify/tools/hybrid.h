/* Hybrid execution: interpreter for untranslated code, C + timing shadow
   for translated routines. See hybrid.c. */
#ifndef SML_HYBRID_H
#define SML_HYBRID_H

#include "../emu/sm83.h"

/* Enable all registered translations (or only those whose names appear in
   `only`, a comma separated list, if not NULL). */
void hybrid_init(Sm83 *interpreter, Machine *m, const char *only);
/* Exec hook installed on the interpreter; calls hybrid_next_exec first. */
int hybrid_chain_exec(Sm83 *c, uint16_t pc, void *user);
extern int (*hybrid_next_exec)(Sm83 *c, uint16_t pc, void *user);
long hybrid_c_calls(void);

#endif
