set -e
#Удалить билд 
rm -rf build
#Сборка 
west build -b promicro_nrf52840/nrf52840/uf2 -d build

#Загрузить 
cp /home/blebyshek/zephyrproject/myapp/fluger/build/zephyr/zephyr.uf2 /media/$USER/NICENANO/

echo "Uploaded"