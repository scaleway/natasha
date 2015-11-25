#ifndef CORE_H_
#define CORE_H_

#ifdef RTE_EXEC_ENV_BAREMETAL
    #define MAIN _main
#else
    #define MAIN main
#endif

/* Configure logging */
#ifdef DEBUG
    #define LOG_LEVEL RTE_LOG_DEBUG
    #define LOG_DEBUG(log_type, fmt, args...) do {	\
        RTE_LOG(DEBUG, log_type, fmt, ##args);		\
    } while (0)
#else
    #define LOG_LEVEL RTE_LOG_INFO
    #define LOG_DEBUG(log_type, fmt, args...) do{} while(0)
#endif

/* Macros for printing using RTE_LOG */
#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

#endif
