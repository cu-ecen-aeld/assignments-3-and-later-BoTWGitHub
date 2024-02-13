#include <stdio.h>
#include <syslog.h>

int main(int argc, char* argv[])
{
    FILE *fp;
    openlog(NULL, 0, LOG_USER);

    if(argc != 3){
        syslog(LOG_ERR, "Invalid number of arguments: %d", argc-1);
        return 1;
    }

    const char *writefile = argv[1];
    const char *writestr  = argv[2];
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    fp = fopen(writefile, "w");
    if(fp == NULL){
        syslog(LOG_ERR, "fopen %s failed", writefile);
        return 1;
    }

    if(fprintf(fp, "%s\n", writestr) < 0){
        syslog(LOG_ERR, "fprintf %s failed", writestr);
        return 1;
    }

    fclose(fp);
    closelog();

    return 0;
}