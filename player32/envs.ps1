# set environment variables
# when transitioning to esp-adf, you have to set the esp-idf variable correctly.
# that means setting it to 

$ESPDIR = $HOME + '\dev\esp\esp-idf\v5.4\esp-idf'

echo "* setting up environment to use $ESPDIR"

# set the esp-idf variable directly
#

echo "* set the persistant enviroment variable"

setx IDF_PATH $ESPDIR | Out-Null

echo "* make sure it's set for me"
$env:ESP_IDF = $ESPDIR

# run the export in that directory 
echo "* run the correct export to get the right compiler paths"
. "$ESPDIR/export.ps1"