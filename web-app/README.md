# I-Shoot Mo web app

Use **Sign up** to create a username and password, **Log in** for an
existing account, or **Play as a Guest**. Start from the app to save the round to your account. **Your stats**
shows recent rounds and throws, and **Your coach** explains the suggestion
for the signed-in player. Guests can play all three single-player games.
A guest can use a nickname for the 60-second leaderboard. Refreshing
clears the guest name from the page. The ranked name and score remain on
the leaderboard, which signed-in players can view.

Choose **VS mode** for two players in **6 Throws** or **60 Seconds**.
Enter both players’ names to unlock Start. Player 1 sets the score to
beat. After Player 2 finishes, the result
shows the winner or a tie. **Play again** starts a fresh match with the
same names and game. **Back to dashboard** clears the match. VS rounds
stay out of player histories and rankings.

**60 Seconds Competition** has unlimited throws. The leaderboard uses
each player’s best completed single-player score in that game. **Color Memory**
starts at Level 1 and uses 3–12 flashes across 10 levels. Watch the lights,
then throw into the holes in that order. Completing the pattern perfectly
unlocks the next level, which flashes faster. A miss or wrong hole means
retrying the same level. The physical Start button uses the selected cabinet
game and records the round as a cabinet guest. For Memory started on the
cabinet, hold Start at the results screen to continue.

Background music loops after a player starts interacting with the game.
Use the speaker icon to mute game audio and **Volume** to adjust the music and effects separately.

Anyone who has joined the game can tap **Mark last hit as miss** beside
the game controls after a scored hit.
This deducts the awarded points and records the original throw as a miss.
The number of throws left stays the same. Corrections to a completed result
update stats and rankings.

## Local preview and verification

From this directory, run `node server/local.js --demo` and open
`http://127.0.0.1:4173/`. Demo throws run on a simulated board and are stored
in memory. Run `node --test tests/game.test.js` to check device controls,
round reports, and the three IS features.

## Live deployment

1. Run `supabase/schema.sql` followed by `supabase/players.sql` in a new
   Supabase project. For an existing I-Shoot Mo database, apply
   `supabase/players.sql` to add accounts, ownership, and rankings.
2. Apply `supabase/live.sql` and set the Vercel environment variables in
   `.env.example`. Use the Supabase project's `sb_publishable_` key for live
   notifications and its `sb_secret_` key for server reads and writes. Set a game access code of at
   least 12 characters, and an independently generated device token of at
   least 32 characters. Keep the secret key, game access code, and device token
   in server and local configuration. If you use a different Supabase project,
   set its WebSocket origin in the `connect-src` directive in `vercel.json`.
3. Deploy this directory to the linked Vercel project. Use the project's
   stable HTTPS URL when configuring the board.
4. In `creation/code/ishootmo_code`, copy `online_config.example.h` to
   `online_config.h`. Enter the board's 2.4 GHz Wi-Fi details, the app URL
   without a trailing slash, and the same device token. The header is ignored
   by Git. Verify and upload `ishootmo_code.ino` for WEMOS LOLIN32.
   On Windows, `creation/code/Configure-WebFirmware.ps1 -AppUrl <app-url>`
   can write the header with a hidden Wi-Fi password prompt.
5. Confirm `WEB: board connected` in Serial Monitor at 115200 baud. Open the
   deployed site, enter the game code, and check one round started from the
   physical board and one started from a phone. Verify the score, a full
   miss, and a reset. Check that a completed round appears in Recent rounds
   and that the three feature cards reflect the expected data.

An unconfigured sketch keeps local hardware play available. The online
sketch keeps a small in-memory queue while Wi-Fi is interrupted, so power
loss during an outage can lose unsent rounds.
The cabinet sends game changes immediately. Supabase Realtime wakes the app
and cabinet to fetch updates and commands through their usual authenticated
routes. Both keep checking periodically during a live connection outage.
