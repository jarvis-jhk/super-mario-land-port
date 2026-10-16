/* Safe automatic updates from the project's GitHub releases. */
#ifndef SML_UPDATE_H
#define SML_UPDATE_H

/* Starts a background check and, when a newer release exists, downloads
   this package's file, verifies its size and SHA-256 and replaces the
   program for the next start. Offline, missing curl, read-only installs and
   development versions are quiet no-ops; the game never waits for it.
   data_dir: writable directory with a trailing separator. */
void update_start(const char *data_dir);

/* A message for the player once the check has something to say, else
   NULL. Returns each message once. Main thread only. */
const char *update_message(void);

#endif
