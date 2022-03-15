###############################################################
# SMTP.py - Ethan Taylor Behar ebehar
# CREATED: 09/3/2021
# 
# A simple SMTP client.
# For P538 Computer Networks Assignment 01_SMTP Task 2
# NOTE
# I didn't implement any error checking or data validation in this assignment.
# I understand that in a real application this would not be acceptable.
# Applications that do not fail gracefully are often hated and avoided.
# Variables that could use data validation are: 
# host, port, sender, receiver, filename/filepath.n 
###############################################################
from src.netcat import Netcat
# Thank you user:Joey for the "src." syntax that I didn't know about
# https://stackoverflow.com/questions/4383571/importing-files-from-different-folder
import datetime

# Constants
HELO_COMMAND = 'HELO '
MAIL_COMMAND = 'MAIL FROM:'
RCPT_COMMAND = 'RCPT TO:'
DATA_COMMAND = 'DATA'
DATA_DELIMITER = '.'
QUIT_COMMAND = 'QUIT'
ONE = 1
RETURN = '\r'
NEW_LINE = '\n'
CARRIAGE_RETURN = RETURN + NEW_LINE

class SimpleSMTPClient(object):
    host, sender, senderDomain, receiver, subject, filename, emailBody = '', '', '', '', '', '', ''
    port = 0
    nc = None

    def __init__(self):
        print('Hello user, are you ready to send mail?')
        print('I will guide you along the way.')
        self.nc = Netcat()

    def run(self):
        self.host = self.AskForHost()
        self.port = self.AskForPort()
        self.sender = self.AskForSenderEmail()
        atIndex = self.sender.index('@') + ONE
        self.senderDomain = self.sender[atIndex:]
        self.receiver = self.AskForReceiverEmail()
        self.subject = self.AskForSubjectTitle()
        self.filename = self.AskForFilename()

        # for testing - just so we can return through the inputs
        # self.host = 'mail-relay.iu.edu'
        # self.port = 25
        # self.sender = 'ebehar@iu.edu'
        # self.receiver = 'ebehar@iu.edu'
        # self.subject = 'Email from SMTP Client'
        # self.filename = 'EmailContents.txt'

        self.emailBody = self.LoadEmailContents()


        print('Let\'s try connecting. One second... (dial-up noises)')
        try:
            self.nc.connect(self.host, self.port)
            print(self.nc.read())
        except:
            print('Uh-oh, that didn\'t work...')
            print('Please double check the host and port you entered.')
            print('HOST = ' + self.host)
            print('PORT19 = ' + self.port)
            return

        heloCommand = HELO_COMMAND + self.senderDomain + CARRIAGE_RETURN
        # print('Sending command: ' + heloCommand)
        self.nc.write(heloCommand)
        print(self.nc.read())

        mailCommand = MAIL_COMMAND + self.sender + CARRIAGE_RETURN
        # print('Sending command: ' + mailCommand)
        self.nc.write(mailCommand)
        print(self.nc.read())

        rcptCommand = RCPT_COMMAND + self.receiver + CARRIAGE_RETURN
        # print('Sending command: ' + rcptCommand)
        self.nc.write(rcptCommand)
        print(self.nc.read())

        dataCommand = DATA_COMMAND + CARRIAGE_RETURN
        # print('Sending command:' + dataCommand)
        self.nc.write(dataCommand)
        print(self.nc.read())
        self.SendEmailContentCommands()

        dataDelimiterCommand = DATA_DELIMITER + CARRIAGE_RETURN
        # print('Sending data delimiter: ' + dataDelimiterCommand)
        self.nc.write(dataDelimiterCommand)
        print(self.nc.read())

        quitCommand = QUIT_COMMAND + CARRIAGE_RETURN
        # print('Sending command ' + quitCommand)
        self.nc.write(quitCommand)
        print(self.nc.read())

        print('Your e-mail was sent successfully. Good-bye.')
        self.nc.close()

    def AskForHost(self):
        print('Please enter ip address or resolvable url host: ')
        userInput = input()
        return userInput

    def AskForPort(self):
        print("Now enter the port: ")
        userInput = input()
        return userInput

    def AskForSenderEmail(self):
        print('Please enter the sender email: ')
        userInput = input()
        return userInput

    def AskForReceiverEmail(seflf):
        print('Please enter the receiver email: ')
        userInput = input()
        return userInput        

    def AskForSubjectTitle(self):
        print('Please enter the subject of the email: ')
        userInput = input()
        return userInput

    def AskForFilename(self):
        print('Please enter the filename containing the context of the email.')
        userInput = input()
        return userInput

    def LoadEmailContents(self):
        fileStream = open(self.filename, "r")
        # fileStream = open(r"D:\Projects\IU-P538-Assignments\Net-Fall21\01_SMTP\src\EmailContents.txt", "r")
        fileContents = fileStream.read()
        fileStream.close()
        return fileContents

    def SendEmailContentCommands(self):
        body = 'Date: ' + str(datetime.datetime.now()) + CARRIAGE_RETURN
        # print('Sending part of email body: ' + body)
        self.nc.write(body)
        body = 'FROM: ' + self.sender + CARRIAGE_RETURN
        # print('Sending part of email body: ' + body)
        self.nc.write(body)
        body = 'SUBJECT: ' + self.subject + CARRIAGE_RETURN
        # print('Sending part of email body: ' + body)
        self.nc.write(body)
        body = 'TO: ' + self.receiver + CARRIAGE_RETURN
        # print('Sending part of email body: ' + body)
        self.nc.write(body)
        body = CARRIAGE_RETURN
        # print('Sending part of email body: ' + body)
        self.nc.write(body)
        body = self.emailBody + CARRIAGE_RETURN
        # print('Sending part of email body: ' + body)
        self.nc.write(body)

def main():
    client = SimpleSMTPClient()
    client.run()

if __name__ == "__main__":
    main()