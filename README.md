# CTFO

### Architecture

1. API: CTFO server => app.
   Requesting and updating cards and their metadata, user status and notifications.
   Two-way handshake: app -> backend API -> app.
   The app owns the request and acts on the response being received. Generally, at most one request is active at a time.


2. Analytics: App => { CTFO server, GA/Mixpanel servers }
   Structured reporting of user activity.
   One-way upload: app -> analytics API.
   CTFO backend is powered by our platform, <href>Current</href>. To upload events, we have an Objective-C library mirroring the interface of Mixpanel / Google Analytics.

On the app side, API and analytics can be thought of as two different backends.
On the server side, the Current platform binds them together.


### Design

The app is offline-friendly. The user may get offline at any moment, and that should not create any blockers for the UX.

Thus:

* a sufficiently large number of cards is precached; no synchronous response from the server is needed to keep swiping.
* card browsing history, accessible via a swipe down, is local to the app / device.
* user engagement events reporting is asynchronous; reporting iOS API calls can be assumed to not return and/or expose callbacks.


### Implementation

API:

* **Auth**: given device/user info, receive a token and cache key.
* **Pull**: given an auth token, cache key and hot/recent setting, receive/update cards, their metadata, and user status+notifications.

Analytics:

* User **Identify** events.
* **Focus** events.
* **CTFO/TFU/TIFB** events.
* Other **Dictionary**-based events.

From app's analytics library standpoint, **CTFO/TFU/TIFB** events form the only dedicated type of structured event beyond standard Mixpanel/GA protocol.


### Personalization

The protocol supports dynamically adjusting the order of cards that have not yet been seen by the user. The design is:

1. **Seen cards in a pile are the "history", where cards have indexes.**

   It's an array, where cards have fixed indexes, ex. `"0", "1", "2", etc.`

2. **Unseen cards are the "pool", where cards have scores.**

   Scores can be thought of as a relevance score or rank order.

   Scores are foating point numbers, ex. `{ card42 -> 0.97, card37 -> 0.89, etc. }`.


3. **Unseen cards get "promoted" to seen one after another.**

   The datastructure exposes one operation: pick the yet unseen card with the top score, remove it from the "pool", push it into the array.


4. **Scores for yet unseen cards can be changed in future API responses.**
   
   If by the time the API sends an updated score for a card, this card has already been seen, its updated score is ignored.

   If the card has not yet been seen, the score from API response with the largest timestamp should be kept.


