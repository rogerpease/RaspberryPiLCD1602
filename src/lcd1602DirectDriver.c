//
// RDP NOTE: I *did* write this code, obviously using other code as a starting point. 
//
//
// This driver Builds a character driver for the LCD1602 module.
//  The  user can do write to an I2C Connected PCM8574 to LCD1602 to display with a command like:    
//
//       echo "Hello World\\This is a test" > /dev/lcd1602-0   
//
//     \\ is the line delimiter
//
// Of course there are other ways to connect to a 1602 but this is for demo/note purposes.
//
//
//

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <linux/delay.h>


#define BANNER "lcd1602"
#define NUM_ROWS (2) 
#define NUM_COLS (24) 


/* This structure will represent single device */
struct lcd1602_dev {
    struct i2c_client * client;
    struct miscdevice lcd1602_miscdevice;  // Kernel will give us file->private-data which points to this. 
    char name[10]; 
    char lines[NUM_ROWS*NUM_COLS]; 
    int Initialized;
};


//
// We assume the user will not read this with incremental read() calls.
//  They will request the whole region. 
//


static ssize_t lcd1602_read_file(struct file *file, char __user *userbuf,
                                  size_t count, loff_t *ppos)
{
	struct lcd1602_dev * lcd1602;
        int lineNum = 0; 

	lcd1602 = container_of(file->private_data,
			       struct lcd1602_dev, 
			       lcd1602_miscdevice);

        
	if(*ppos == 0)
        {
            for (lineNum = 0; lineNum < NUM_ROWS; lineNum ++) 
            { 
	        pr_info("Writing back %s\n",lcd1602->lines);
		if (copy_to_user(userbuf, lcd1602->lines, strlen(lcd1602->lines))){
			pr_info("Failed to return led_value to user space\n");
			return -EFAULT;
		}
		*ppos+=1;
            } 
  	    return strlen(lcd1602->lines);
	}

	return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ECYCLE cycles the rs bit.. low to high to low (bit 4). Two of these make up 1 8-bit register write. 
//
//
static ssize_t ECycle (struct lcd1602_dev * lcd1602,int dw,int rw,int rs,int led) 
{ 
       int ret, WriteByte; 

       WriteByte = (dw << 4) + (led<<3) + (0<<2) + (rw<<1) + (rs<<0); // Set RW and RS
       dev_info(&lcd1602->client->dev, "Writing %d to I2C \n",WriteByte);

       ret = i2c_smbus_write_byte(lcd1602->client, WriteByte);
       if (ret < 0) { dev_err(&lcd1602->client->dev, "ECYCLE Pass1 failed!\n"); return 0; } 
       mdelay(1);

       WriteByte |=  0x4;  // Raise 2 bit. 
       ret = i2c_smbus_write_byte(lcd1602->client, WriteByte);
       if (ret < 0) { dev_err(&lcd1602->client->dev, "ECYCLE Pass2 failed!\n"); return 0;} 
       mdelay(1);

       WriteByte &= ~0x4; 
       ret = i2c_smbus_write_byte(lcd1602->client, WriteByte);
       if (ret < 0) { dev_err(&lcd1602->client->dev, "ECYCLE Pass3 failed!\n"); return 0;} 
       mdelay(1);

       return 1; 

}

/////////////////////////////////////////////////////////////////////////////////
//
// Write out an instruction (as opposed to write out a letter). 
//
//
static ssize_t WriteInstruction(struct lcd1602_dev * lcd1602,int dataWord)
{
      int upperNib,lowerNib;
      upperNib = ((dataWord >> 4) & 0xF);
      lowerNib = ((dataWord     ) & 0xF);
      if (ECycle(lcd1602,upperNib,0,0,1) && ECycle(lcd1602,lowerNib,0,0,1)) { return 1; } 
      return 0; 
}

/////////////////////////////////////////////////////////////////////////////////
//
// Set Cursor Position 
//
static ssize_t SetCursor(struct lcd1602_dev * lcd1602,int line,int col)
{
      int setDdramAddress;

      pr_info(BANNER " Set Cursor %d %d",line,col); 
      // Set DDRAM Address.. Bit 7 is 1, address is line * 0x40 + col
      setDdramAddress = 0x40*line + col + (1<<7);

      if (WriteInstruction(lcd1602, setDdramAddress)) {return 1;} 
      return 0; 

}


static ssize_t WriteLetter(struct lcd1602_dev * lcd1602,char letter)
{
  
      int upperNib = ((letter >> 4) & 0xF);
      int lowerNib = ((letter)      & 0xF); 

      pr_info(BANNER " Write Letter %c",letter); 

      if (ECycle(lcd1602,upperNib,0,1,1) && ECycle(lcd1602,lowerNib,0,1,1)) { return 1;} 

      return 0; 

}

static ssize_t Initialize(struct lcd1602_dev * lcd1602)
{

      pr_info(BANNER " Initialize "); 

      // Thanks to http://www.site2241.net/november2014.htm for de-muddying the datasheet.
                    // DW  RW   RS  LED 
      if (lcd1602->Initialized == 0) 
      { 
        if (ECycle(lcd1602,0x2,0,0,1) && ECycle(lcd1602,0x2,0,0,1) && ECycle(lcd1602,0x8,0,0,1)) 
        { 
           lcd1602->Initialized = 1;
        } 
        else 
        {
           return -1; 
        }
      } 

      if ((ECycle(lcd1602,0x0,0,0,1)) && // Set 4-bit mode
          (ECycle(lcd1602,0xC,0,0,1)) && // Set DL=0 L N Mode
          (ECycle(lcd1602,0x0,0,0,1)) && // Set N=0 F=0 Mode
          (ECycle(lcd1602,0x6,0,0,1)) && // Set DL=0 L N Mode
          (ECycle(lcd1602,1,0,0,1))   && // First Line, 1st position
          (ECycle(lcd1602,1,0,0,1)))
        return 1; 
      return -1; 

}



static ssize_t SetDisplay(struct lcd1602_dev * lcd1602) 
{
   int row = 0; 
   int col = 0; 
   int lineIndex = 0; 
   int endOfRow = 0; 
   char character = 0; 
   int failed = 0; 

   if (Initialize(lcd1602) != 1) { return 0; }

   while ((row < NUM_ROWS) && (!failed))
   { 
     col = 0; 
     SetCursor(lcd1602,row,col);
     endOfRow = 0; 
     while ((col < NUM_COLS) && (!failed))
     { 
        if (!endOfRow) 
           character = lcd1602->lines[lineIndex++]; 
        if ((!endOfRow) && (character != '\\') && (character != '\n') && (character != 0)) 
        { 
          if (WriteLetter(lcd1602,character) == 0) return 0;  
        } 
        else 
        {
          endOfRow = 1; 
          if (WriteLetter(lcd1602,32) == 0) return 0; 
        } 
        col++; 
     } 
     row++; 
   } 
  
   return 1; 
}


/* Writing from the terminal command line, \n is added */
static ssize_t lcd1602_write_file(struct file *file, const char __user *userbuf,
                                   size_t count, loff_t *ppos)
{
      int i; 
      struct lcd1602_dev * lcd1602;

      lcd1602 = container_of(file->private_data,
			     struct lcd1602_dev, 
			     lcd1602_miscdevice);

      // Print Debug information. 
      dev_info(&lcd1602->client->dev, 
		 "lcd1602_write_file entered on %s\n", lcd1602->name);

      for (i=0;i<NUM_ROWS*NUM_COLS;i++) 
         lcd1602->lines[i] = 0; 

      if(copy_from_user(lcd1602->lines, userbuf, count)) 
      {
        dev_err(&lcd1602->client->dev, "Bad copied value\n");
        return -EFAULT;
      }

      dev_info(&lcd1602->client->dev, "lcd1602_write_file incoming message %s\n", lcd1602->lines);

      if (SetDisplay(lcd1602))  
        return count;

      return -EFAULT;

}

//
// RDP: This is the file_operations once the file opens. Other functions could include open and close. 
// 
static const struct file_operations lcd1602_fops = {
	.owner = THIS_MODULE,
	.read = lcd1602_read_file,
	.write = lcd1602_write_file,
};

// 
// RDP This is what is called at powerup when the device finds a device-tree entry with "compatible = " which matches the 
//   "compatible = " this is aligned to. 
//
static int lcd1602_probe(struct i2c_client * client,
		const struct i2c_device_id * id)
{
    static int counter = 0;
    struct lcd1602_dev * lcd1602;
 
    dev_info(&client->dev,  "lcd1602_probe called\n" );

    /* Allocate new structure representing device */
    lcd1602 = devm_kzalloc(&client->dev, sizeof(struct lcd1602_dev), GFP_KERNEL);

    /* Store pointer to the device-structure in bus device context */
    i2c_set_clientdata(client,lcd1602);

    /* Store pointer to I2C device/client */
    lcd1602->client = client;

    /* Initialize the misc device, lcd1602 incremented after each probe call */
    sprintf(lcd1602->name, "lcd1602%02d", counter++); 
    dev_info(&client->dev, "lcd1602_probe is entered on %s\n", lcd1602->name);

    lcd1602->lcd1602_miscdevice.name = lcd1602->name;
    lcd1602->lcd1602_miscdevice.minor = MISC_DYNAMIC_MINOR;
    lcd1602->lcd1602_miscdevice.fops = &lcd1602_fops;

    /* Register misc device */
    return misc_register(&lcd1602->lcd1602_miscdevice);

    dev_info(&client->dev, "lcd1602_probe is exited on %s\n", lcd1602->name);

    return 0;
}

static int lcd1602_remove(struct i2c_client * client)
{
    struct lcd1602_dev * lcd1602;

    /* Get device structure from bus device context */	
    lcd1602 = i2c_get_clientdata(client);

    dev_info(&client->dev, "lcd1602_remove is entered on %s\n", lcd1602->name);

    /* Deregister misc device */
    misc_deregister(&lcd1602->lcd1602_miscdevice);
    
    //	
    // RDP: The memory for the module will get deallocated because we allocated it with devm_kzalloc. 
    //	

    dev_info(&client->dev, "lcd1602_remove is exited on %s\n", lcd1602->name);

    return 0;
}



//
//  RDP: This makes sure probe will get called when we show a dts entry with this compatible entry. 
// 

static const struct of_device_id lcd1602_dt_ids[] = {
	{ .compatible = "arrow,lcd1602", },
	{ }
};
MODULE_DEVICE_TABLE(of, lcd1602_dt_ids);

static const struct i2c_device_id i2c_ids[] = {
	{ .name = "lcd1602", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, i2c_ids);

static struct i2c_driver lcd1602_driver = {
	.driver = {
		.name = "lcd1602",
		.owner = THIS_MODULE,
		.of_match_table = lcd1602_dt_ids,
	},
	.probe = lcd1602_probe,
	.remove = lcd1602_remove,
	.id_table = i2c_ids,
};

module_i2c_driver(lcd1602_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Roger Pease <rogerpease@gmail.com>");
MODULE_DESCRIPTION("A simple module for echoing to a lcd1602 -over- pcm8574 device.");


//
// Normally I would test subfunctions here but this is a simple enough module.  
//  We could also do more thorough testing of error conditions, because we really can't force those in memory. 
//  Methods include: 
//    QEMU 
//    Fault injection 
//    Separating out user code with separate tests. 
//



