//=======================================================================
// Copyright Baptiste Wicht 2013-2016.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//=======================================================================

#include <tlib/file.hpp>
#include <tlib/system.hpp>
#include <tlib/errors.hpp>
#include <tlib/print.hpp>

int main(int argc, char* argv[]){
    if(argc == 1){
        tlib::print_line("Usage: stat file_path");
        return 1;
    }

    auto fd = tlib::open(argv[1]);

    if(fd.valid()){
        auto info = tlib::stat(*fd);

        if(info.valid()){
            if(info->flags & tlib::STAT_FLAG_DIRECTORY){
                tlib::print("Directory ");
            } else {
                tlib::print("File ");
            }

            tlib::print_line(argv[1]);

            tlib::printf("Size: %m\n", info->size);
            tlib::print("Flags: ");

            if(info->flags & tlib::STAT_FLAG_HIDDEN){
                tlib::print("Hidden ");
            }

            if(info->flags & tlib::STAT_FLAG_SYSTEM){
                tlib::print("System ");
            }

            tlib::print_line();

            tlib::print("Created: ");

            tlib::print(info->created.day);
            tlib::print('.');
            tlib::print(info->created.month);
            tlib::print('.');
            tlib::print(info->created.year);
            tlib::print(' ');

            tlib::print(info->created.hour);
            tlib::print(':');
            tlib::print(info->created.minutes);
            tlib::print_line();

            tlib::print("Modified: ");

            tlib::print(info->modified.day);
            tlib::print('.');
            tlib::print(info->modified.month);
            tlib::print('.');
            tlib::print(info->modified.year);
            tlib::print(' ');

            tlib::print(info->modified.hour);
            tlib::print(':');
            tlib::print(info->modified.minutes);
            tlib::print_line();

            tlib::print("Accessed: ");

            tlib::print(info->accessed.day);
            tlib::print('.');
            tlib::print(info->accessed.month);
            tlib::print('.');
            tlib::print(info->accessed.year);
            tlib::print(' ');

            tlib::print(info->accessed.hour);
            tlib::print(':');
            tlib::print(info->accessed.minutes);
            tlib::print_line();
        } else {
            tlib::printf("stat: error: %s\n", std::error_message(info.error()));
        }

        tlib::close(*fd);
    } else {
        tlib::printf("stat: error: %s\n", std::error_message(fd.error()));
    }

    return 0;
}
