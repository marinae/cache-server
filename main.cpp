/* Mac OS X */

#include "server.h"
#include <iostream>

//+----------------------------------------------------------------------------+
//| Main                                                                       |
//+----------------------------------------------------------------------------+

int main() {

    /* Create server */
    Server srv;
    if (srv.configure() == -1) {
        printf("error: configuring server failed\n");
        return -1;
    }
    
    /* Start event loop */
    srv.start();

    return 0;
}