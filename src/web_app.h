#ifndef WEB_APP_H_
#define WEB_APP_H_

#define WEB_APP_HTTP_PORT 80

/* Starts the HTTP listener; OTA test images self-confirm only after /api/status is served. */
int web_app_start(void);

#endif /* WEB_APP_H_ */
