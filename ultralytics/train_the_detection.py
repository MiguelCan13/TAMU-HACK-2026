### teehee, really useful:
### https://docs.ultralytics.com/modes/track/
###
### Model has gone through: 100 epochs
###
from ultralytics import YOLO


if __name__ == '__main__':
    #test = model.track(
    #   source = 0,  #use personal webcam
    #   conf = .25,  #confidence threshold
    #   show = True  #display results
    # )
    
    ###CHANGE MODEL IF YOU ALREADY HAVE A PRETRAINED ONE, otherwise use "yolo11n-cls.pt" for classification tasks
    model = YOLO("yolo11n.pt")  

    # Train the model
    results = model.train(
        data="ultralytics/car data for detection/data.yaml",
        epochs=100,              # Number of training epochs (cycles)
        imgsz=640,              # Detection models typically use 640
        name="car_detection",   # Name of the training run
        save=True,              # Save checkpoints
        plots=True,             # Generate training plots
        device=0,               # Use GPU 0 (change to 'cpu' if no GPU)
    )

    # Save the trained model
    model.save("car_detection_model.pt")

    print("\n" + "="*50)
    print("Training Complete!")
    print(f"Best model saved at: runs/detect/car_detection/weights/best.pt")
    print("="*50)
