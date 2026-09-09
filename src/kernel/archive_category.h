// Категоризація збережених елементів (звітів) за доменом — аналог FlipperKeyType.
// Flipper: кожен збережений «ключ» типізований (subghz/lfrfid/nfc/infrared/ibutton) і
// Archive гортається за типом. У нас звіти були ПЛОСКІ; ця чиста функція мапить slug
// звіту -> домен-категорію, щоб Archive (плата/веб/мобілка) гортався за типом.
// БЕЗ апаратних включень -> native-тести.
#pragma once

// Повертає стабільний рядок-категорію для імені/slug звіту:
//   "subghz" | "rfid" | "wifi" | "bluetooth" | "network" | "detect" | "system" | "misc"
// Ніколи не nullptr. Регістронезалежно за префіксами.
const char* archive_category(const char* slug);
