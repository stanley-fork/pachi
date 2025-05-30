#ifndef PACHI_VERSION_H
#define PACHI_VERSION_H

#include "build.h"

#define PACHI_VERNUM 12.86
#define PACHI_VERNUMS "12.86"

/* 00. Wang Zhi
 * 01. Sansa     Meijin  1612-1623
 * 02. Sanetsu   8-Dan   1630-1658
 * 03. Doetsu    7-Dan   1658-1677
 * 04. Dosaku    Meijin  1677-1702
 * (h) Doteki    7-Dan  (1684-1690)
 * (h) Sakugen   7-Dan  (1692-1699)
 * 05. Dochi     Meijin  1702-1727
 * 06. Chihaku   6-Dan   1727-1733
 * 07. Shuhaku   6-Dan   1733-1741
 * 08. Hakugen   6-Dan   1741-1754
 * 09. Satsugen  Meijin  1754-1788
 * 10. Retsugen  8-Dan   1788-1808
 * 11. Genjo     8-Dan   1808-1827
 * 12. Jowa      Meijin  1827-1839
 * 13. Josaku    7-Dan   1839-1847
 * 14. Shuwa     8-Dan   1847-1873
 * (h) Shusaku   7-Dan  (1848-1862)
 * 15. Shuetsu   6-Dan   1873-1879
 * 16. Shugen    4-Dan   1879-1884
 * 17. Shuei     7-Dan   1884-1886
 * 18. Shuho     8-Dan   1886
 * 19. Shuei     Meijin  1887-1907
 * 20. Shugen    6-Dan   1907-1908
 * 21. Shusai    Meijin  1908-1940 */
#define PACHI_VERNAME "Jowa"

#define PACHI_VERSION       PACHI_VERNUMS
#define PACHI_VERSION_FULL  PACHI_VERNUMS " (" PACHI_VERNAME ")"
#define PACHI_VERGIT        PACHI_GIT_HASH " (" PACHI_GIT_BRANCH ")"

#ifdef DCNN
    #define PACHI_VERBUILD PACHI_BUILD_TARGET " dcnn build, " PACHI_BUILD_DATE
#else
    #define PACHI_VERBUILD PACHI_BUILD_TARGET " !dcnn build, " PACHI_BUILD_DATE
#endif

#endif
