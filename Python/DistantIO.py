# Copyright (C) 2014 Rémi Bèges
# For conditions of distribution and use, see copyright notice in the LICENSE file

from queue import Queue
import struct
from pubsub import pub

# DistantIO class : to read and write variables on the MCU from distant computer
# TODO : parse all datatype

class DistantIO():

    def __init__(self):
        self.log_table = Queue(0)
        self.variables = list()
        self.type_lookup = {0 : '=f',
                            2 : '=H',
                            3 : '=I',
                            6 : '=i'
                            }
        self.size_lookup = {0 : 4,
                            2 : 2,
                            3 : 4,
                            6 : 4}
        pub.subscribe(self.decode,'new_rx_payload')

    #Process RX bytes queue
    def decode(self,rxpayload):
        frame = rxpayload
        index = 0
        command = frame[0]
        datatype = frame[1] & 0x0F

        temp2 = bytearray()
        temp2.insert(1,frame[2])
        temp2.insert(1,frame[3])
        temp2.insert(1,0)
        temp2.insert(1,0)
        dataid = struct.unpack("=i",temp2)[0]
        
        # Parse 'received_variable_value' payload
        if command == 0:
            #int
            if datatype == 6:
                new_values = list()
                index = 4
                
                if (len(frame) - 4) < 4:
                    print("Unvalid frame size")
                    return
                
                while len(frame) - index >= 4:
                    # Stock raw bytes in byte array
                    temp = bytearray()
                    temp.insert(1,frame[index])
                    temp.insert(1,frame[index+1])
                    temp.insert(1,frame[index+2])
                    temp.insert(1,frame[index+3])
                    index += 4
                   
                    # Get format
                    fmt = self.type_lookup[datatype]
                    
                    # Transform value to desired format
                    val = struct.unpack(fmt,temp)[0]
                    
                    # Store to list
                    new_values.append(val)
                    
                #Publish the value update
                pub.sendMessage('var_value_update',varid=dataid,value_list=new_values)

                
            #float
            elif datatype == 0:
                new_values = list()
                index = 4
                
                if (len(frame) - 4) < 4:
                    print("Unvalid frame size")
                    return
                
                while len(frame) - index >= 4:
                    # Stock raw bytes in byte array
                    temp = bytearray()
                    temp.append(frame[index])
                    temp.append(frame[index+1])
                    temp.append(frame[index+2])
                    temp.append(frame[index+3])
                    index += 4

                    # Get format
                    fmt = self.type_lookup[datatype]
                    
                    # Transform value to desired format
                    val = struct.unpack(fmt,temp)[0]
                    
                    # Store to list
                    new_values.append(val)
                
                #Publish the value update
                pub.sendMessage('var_value_update',varid=dataid,value_list=new_values)
            #############    
            #uint32
            elif datatype == 3:
                new_values = list()
                index = 4
                
                if (len(frame) - 4) < 4:
                    print("Unvalid frame size")
                    return
                
                while len(frame) - index >= 4:
                    # Stock raw bytes in byte array
                    temp = bytearray()
                    temp.append(frame[index])
                    temp.append(frame[index+1])
                    temp.append(frame[index+2])
                    temp.append(frame[index+3])
                    index += 4

                    # Get format
                    fmt = self.type_lookup[datatype]
                    
                    # Transform value to desired format
                    val = struct.unpack(fmt,temp)[0]
                    
                    # Store to list
                    new_values.append(val)
                
                #Publish the value update
                pub.sendMessage('var_value_update',varid=dataid,value_list=new_values)
            #############    
            #uint16
            elif datatype == 2:
                new_values = list()
                index = 4
                
                if (len(frame) - 4) < 4:
                    print("Unvalid frame size")
                    return
                
                while len(frame) - index >= 4:
                    # Stock raw bytes in byte array
                    temp = bytearray()
                    temp.append(frame[index])
                    temp.append(frame[index+1])
                    index += 2

                    # Get format
                    fmt = self.type_lookup[datatype]
                    
                    # Transform value to desired format
                    val = struct.unpack(fmt,temp)[0]
                    
                    # Store to list
                    new_values.append(val)
                
                #Publish the value update
                pub.sendMessage('var_value_update',varid=dataid,value_list=new_values)  
        # Parse 'received_table' payload
        elif command == 2:
            #Empty list
            self.variables = list()
            index = 4
            while len(frame) - index >= 37:
                # Read datatype
                databyte = frame[index]
                datatype = databyte & 0x0F
                index+=1
                
                # Read rights
                write_rights = (databyte >> 4) == 0x0F
                
                # Read variable ID
                temp = bytearray()
                temp.append(frame[index])
                temp.append(frame[index+1])
                index += 2
                
                varid = struct.unpack('H',temp)[0]

                # Read variable size in octets
                temp = bytearray()
                temp.append(frame[index])
                temp.append(frame[index+1])
                index += 2
                
                octets = struct.unpack('H',temp)[0]

                # TODO : Compute array size if array
                
                # Read name
                name = ""
                
                #32 characters max
                for x in range(0,32):
                    c = str(chr(frame[index]))
                    name += c
                    index += 1

                #Put everything in tuple
                t = varid, datatype, octets, write_rights, name

                #Stock the tuple in the variable list
                self.variables.append(t)
                
            #If successful, publish new table
            pub.sendMessage('logtable_update',varlist=self.variables)
        else:
            print("Logger : unknown MCU answer : ",command)

        pass

    #Command for asking the MCU the loggable variables 
    def encode(self, cmd, var_id=0 ,value=0): 
        frame = bytearray()
    
        if cmd == 'table':
            frame.append(int('02',16))
            frame.append(int('07',16))
            frame.append(int('00',16))
            
        elif cmd == 'read':        
            frame = bytearray()
            frame.append(int('00',16))
            frame.append(int('00',16))#IGNORED ?
            packed = bytes(struct.pack('=H',var_id))
            frame.extend(packed)
            
        elif cmd == 'write':
            # Check var is in list
            # TODO : Actually check against id values directly
            if var_id >= len(self.variables):
                return
            
            if not self.variables[var_id][0] == var_id:
                return

            # Check rights
            if not self.variables[var_id][3] == 1:
                print("Not rights to write variable.")
                return

            # Find type
            fmt = self.variables[var_id][1]
        
            frame.append(int('01'))
            frame.append(int(fmt))

            packed = bytes(struct.pack('=H',var_id))
            frame.extend(packed)
        
            # Parse double to type
            val = None
            if fmt == 0:
                val = float(value)
                packed = bytes(struct.pack('=f',val))
            elif fmt == 6:
                val = int(value)
                packed = bytes(struct.pack('=i',val))
            else:
                print("Write format not supported.")
                return
            
            frame.extend(packed)       
        else:
            return
        
        return frame

    def get_var_list(self):
        return self.variables

    def get_var_info(self,varid):
        return self.variables[var_id]
