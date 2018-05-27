chat: chat.c
	gcc chat.c -o chat

shell:	shell.c
	gcc shell.c -o shell

shell-readline: shell.c
	gcc shell.c -o shell-readline -lreadline -DREADLINE

clean:
	rm shell shell-readline chat
