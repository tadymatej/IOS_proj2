

christmasStarted=False
workshopClosed=False
santaHelping=False
NeedHelp3Plus=False
santaSleeping=False
getHelpCount=0

f = open("/home/matej/VUT/IOS/projekt2/testy/proj2.out", "r")
for line in f.readlines():
    if "Santa" in line:
        if "closing workshop" in line:
            if workshopClosed or not santaSleeping: #Pomohl skřítkům a nešel spát
                print("Chyba na řádku: {}".format(line))
            workshopClosed=True
        elif "Christmas started" in line:
            if christmasStarted or not workshopClosed or not santaSleeping:  #2x vánoce nebo vánoce bez closed workshop
                print("Chyba na řádku: {}".format(line))
            christmasStarted=True

        elif "going to sleep" in line:
            if santaSleeping:
                print("Chyba na řádku: {}".format(line))
            santaSleeping=True
            santaHelping=False
        if "helping elves" in line:
            if santaHelping:
                print("Chyba na řádku: {}".format(line))
            santaSleeping=False
            santaHelping=True
            getHelpCount=0
    
    elif "Skřítek" in line:
        if christmasStarted:
            if not ("taking holidays" or "need help") in line:
                print("Chyba na řádku: {}".format(line))
        elif workshopClosed:
            if "get help" in line:
                print("Chyba na řádku: {}".format(line))
        elif santaSleeping:
            if "get help" in line:
                print("Chyba na řádku: {}".format(line))
        elif not workshopClosed:
            if "taking holidays" in line:
                print("Chyba na řádku: {}".format(line))
        if "get help" in line:
            getHelpCount+=1
            if getHelpCount > 3:
                print("Chyba na řádku: {}".format(line))
    elif "Sob" in line:
        if "get hitched" in line:
            if(not workshopClosed):
                print("Chyba na řádku: {}".format(line))
            if christmasStarted:
                print("Chyba na řádku: {}".format(line))
f.close()