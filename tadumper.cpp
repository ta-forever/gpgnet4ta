#include <cctype>
#include <iostream>
#include <iomanip>
#include <map>
#include <set>
#include <fstream>
#include <sstream>

#include "tademo/GameMonitor.h"

int main(int argc, char **argv)
{
    for (const char *fn : {
        R"(D:\games\TA\Recorded Games\23_10_2020 - Crystal Maze - TheCoreCommander, TheArmCommander - nr 4.tad)",

        R"(D:\games\TA\Recorded Games\ally_unally_TACquits - TCC.tad)",
        R"(E:\SHARE\m1demos\ally_unally_TACquits - TAC.tad)",

        R"(D:\games\TA\Recorded Games\ally in lobby - TCC.tad)",
        R"(E:\SHARE\m1demos\ally in lobby - TAC.tad)",

        R"(D:\games\TA\Recorded Games\TAC disconnects - TCC.tad)",
        R"(E:\SHARE\m1demos\TAC disconnects - TAC.tad)",

        R"(D:\games\TA\Recorded Games\spawnradar(TAC),selfdradar(TAC),selfdcom(TCC) - TCC.tad)",
        R"(E:\SHARE\m1demos\spawnradar(TAC),selfdradar(TAC),selfdcom(TCC) - TAC.tad)"
    })
    {
        std::cout << "---------------" << std::endl;
        std::cout << fn << std::endl;
        GameMonitor monitor;
        std::ifstream fs(fn, std::ios::in | std::ios::binary);
        while (!fs.eof())
        {
            try
            {
                monitor.parse(&fs);

                if (monitor.isGameStarted())
                {
                    std::cout << "GAME STARTED" << std::endl;
                }

                if (monitor.isGameOver())
                {
                    std::cout << "GAME OVER" << std::endl;
                    monitor.getGameResult().print(std::cout);
                }
            }
            catch (std::runtime_error &e)
            {
                std::cerr << e.what();
            }
        }
    }
}

