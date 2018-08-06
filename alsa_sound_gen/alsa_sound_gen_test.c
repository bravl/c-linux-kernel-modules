#include <stdio.h>
#include <stdlib.h>

#include <alsa/asoundlib.h>

int main()
{
        int id = -1, err;
        char *name, *longname;

        for (;;) {
                err = snd_card_next(&id);
                if (id < 0 || err < 0) {
                        printf("No next sound card\n");
                        break;
                }
                if ((err = snd_card_get_name(id, &name)) < 0) {
                        printf("Failed to get name\n");
                        continue;
                }
                if ((err = snd_card_get_longname(id, &longname)) < 0) {
                        printf("Failed to get longname\n");
                        continue;
                }
                printf("Soundcard: %s\n", name);
                printf("longname: %s\n", longname);
        } 
        return 0;
}
