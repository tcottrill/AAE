CURRENT is the current value for that input, Y or X

DELTA, is the SCALED change per read, X or Y


UPDATE

	for (port = 0;port < MAX_INPUT_PORTS;port++)
	{
		if (input_vblank[port])
		{
			input_port_value[port] ^= input_vblank[port];
			input_vblank[port] = 0;
		}
	}
	/* update mouse position */
	mouse_previous_x = mouse_current_x;
	mouse_previous_y = mouse_current_y;
	osd_trak_read(&deltax,&deltay);
	mouse_current_x += deltax;
	mouse_current_y += deltay;
}



  get input + or -

	
	/* extremes can be either signed or unsigned */
	if (min > max) min = min - 256;

 SET MINIMUM VALUE	

	current = input_analog_value[port];

	if (axis == X_AXIS) //X
	{
		int now;

		now = cpu_scalebyfcount(mouse_current_x - mouse_previous_x) + mouse_previous_x;
		delta = now - mouse_last_x;
		
	}
	else            //Y
	{
		int now;

		now = cpu_scalebyfcount(mouse_current_y - mouse_previous_y) + mouse_previous_y;
		delta = now - mouse_last_y;
		
	}


	if (clip != 0)
	{
		if (delta*sensitivity/100 < -clip)
			delta = -clip*100/sensitivity;
		else if (delta*sensitivity/100 > clip)
			delta = clip*100/sensitivity;
	}

	if (in->type & IPF_REVERSE) delta = -delta;    //REVERSE IF NECESSARY HERE

	if (is_stick)
	{
		if ((delta == 0) && (in->type & IPF_CENTER))
		{
			if (current > default_value)
				delta = -100 / sensitivity;
			if (current < default_value)
				delta =  100 / sensitivity;
		}


	current += delta;

	if (check_bounds)
	{
		if (current*sensitivity/100 < min)
			current=min*100/sensitivity;
		if (current*sensitivity/100 > max)
			current=max*100/sensitivity;
	}

	input_analog_value[port]=current;

	input_port_value[port] &= ~in->mask;
	input_port_value[port] |= (current * sensitivity / 100) & in->mask;

