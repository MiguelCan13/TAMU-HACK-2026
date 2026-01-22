### teehee, really useful:
### https://docs.ultralytics.com/modes/track/
###
### Model has gone through: 93 epochs
###
from ultralytics import YOLO


if __name__ == '__main__':
    #test = model.track(
    #   source = 0,  #use personal webcam
    #   conf = .25,  #confidence threshold
    #   show = True  #display results
    # )
    
    ###CHANGE MODEL IF YOU ALREADY HAVE A PRETRAINED ONE, otherwise use "yolo11n-cls.pt" for classification tasks
    model = YOLO("yolo11n-cls.pt")  

    # Train the model
    results = model.train(
        data="ultralytics/car orientation data for classification",
        epochs=5,               # Number of training epochs (cycles)
        imgsz=224,               
        name="car_orientation",   # Name of the training run
        save=True,               # Save checkpoints
        plots=True,              # Generate training plots
        device=0,                # Use GPU 0 (change to 'cpu' if no GPU)
    )

    # Save the trained model CHANGE THIS IF YOUR MAKING A NEW VERSION
    model.save("car_orientation_model_normal.pt")

    print("\n" + "="*50)
    print("Training Complete!")
    print(f"Best model saved at: runs/classify/car_orientation/weights/best.pt")
    print("="*50)
