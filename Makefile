all: paster

paster: paster.c starter/png_util/crc.c starter/png_util/lab_png.c starter/png_util/zutil.c
	g++ -o paster paster.c starter/png_util/crc.c starter/png_util/lab_png.c starter/png_util/zutil.c -lcurl -pthread -lz -g

clean:
	rm -rf paster *.o *.out *.png
