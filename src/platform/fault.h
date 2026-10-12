#ifndef SML_FAULT_H
#define SML_FAULT_H
/* Anonymous reports contain fixed categories only, never logs or ROM data. */
void fault_install(void);
void fault_report(void);
#endif
