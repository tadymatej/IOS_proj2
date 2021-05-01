

bracketOpened = False
brackets = 0

with open("/home/matej/VUT/IOS/projekt2/proj2.c", "r") as f:
    with open("/home/matej/VUT/IOS/projekt2/proj2-instrumented.c", "w") as f2:
        lineNum=0
        for line in f.readlines():
            lineNum+=1
            f2.write(line)
            if "{" in line:
                if("createProcesses" in line or "Sob" in line or "Skritek" in line or "skritekTakeHolidays" in line or "Santa" in line or "writeToFile" in line) and not "//" in line:
                    bracketOpened = True
                brackets +=1
            if "}" in line:
                brackets -= 1
                if(brackets == 0):
                    bracketOpened = False
            if bracketOpened and not lineNum == 252 and not lineNum == 373 and not lineNum == 380 and not lineNum == 441 and not lineNum == 496:
                tabs = ""
                for i in range(0, brackets):
                    tabs +="\t"
                f2.write("{}sleepRandom();\n".format(tabs))

