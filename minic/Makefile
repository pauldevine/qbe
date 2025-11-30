BIN = minic

CFLAGS += -g -Wall

$(BIN): yacc minic.y
	./yacc minic.y
	$(CC) $(CFLAGS) -o $@ y.tab.c

minic_cpp: minic_cpp.c minic_preprocess.c minic_tokenize.c minic_support.c minic_token.h
	$(CC) $(CFLAGS) -o $@ minic_cpp.c minic_preprocess.c minic_tokenize.c minic_support.c

preprocess_test: preprocess_test.c minic_preprocess.c minic_tokenize.c minic_support.c minic_token.h
	$(CC) $(CFLAGS) -o $@ preprocess_test.c minic_preprocess.c minic_tokenize.c minic_support.c

tokenize_test: tokenize_test.c minic_preprocess.c minic_tokenize.c minic_support.c minic_token.h
	$(CC) $(CFLAGS) -o $@ tokenize_test.c minic_preprocess.c minic_tokenize.c minic_support.c

all: $(BIN) minic_cpp

clean:
	rm -f yacc minic minic_cpp y.* preprocess_test tokenize_test

.PHONY: clean all
