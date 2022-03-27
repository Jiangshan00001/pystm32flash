// this header file should be carefully write as cffi will use it without preprocess command.
//#ifndef LIBMAIN_H
#define LIBMAIN_H 1
void show_help();
int reset_params();
int set_arg(char * arg_key, char *arg_val);
int set_device(char * device_name);
int run_it();
//#endif

