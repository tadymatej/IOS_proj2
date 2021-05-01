check() {
	echo "$1"
}

runProgram() {
	../proj2 5 5 1 1 | check
}

runProgram
echo "Mykonec"
